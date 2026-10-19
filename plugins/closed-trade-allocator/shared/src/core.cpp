#include "closedalloc/core.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace closedalloc {
namespace {
constexpr std::size_t max_frame = 8 * 1024 * 1024;
constexpr std::size_t max_journal = 512 * 1024 * 1024;
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
template<class T> T number(const std::string& text) {
  T out{};
  const auto r = std::from_chars(text.data(), text.data() + text.size(), out);
  if (text.empty() || r.ec != std::errc{} || r.ptr != text.data() + text.size())
    fail("invalid integer encoding");
  return out;
}
std::uint64_t checksum(const std::string& data) {
  std::uint64_t h = 14695981039346656037ULL;
  for (unsigned char c : data) { h ^= c; h *= 1099511628211ULL; }
  return h;
}
std::uint64_t mix(std::uint64_t n) {
  n += 0x9e3779b97f4a7c15ULL;
  n = (n ^ (n >> 30)) * 0xbf58476d1ce4e5b9ULL;
  n = (n ^ (n >> 27)) * 0x94d049bb133111ebULL;
  return n ^ (n >> 31);
}
bool boolean(Decoder& d) {
  auto n = d.u64(); if (n > 1) fail("invalid boolean encoding"); return n != 0;
}
void put_config(Encoder& e, const Config& c) {
  e.text(c.platform); e.text(c.server); e.u64(c.source); e.u64(c.target);
  e.real(c.loss_probability); e.real(c.profit_probability);
  e.i64(c.enabled_since); e.u64(c.enabled); e.u64(c.armed);
}
Config get_config(Decoder& d) {
  Config c; c.platform=d.text(); c.server=d.text(); c.source=d.u64(); c.target=d.u64();
  c.loss_probability=d.real(); c.profit_probability=d.real(); c.enabled_since=d.i64();
  c.enabled=boolean(d); c.armed=boolean(d); validate(c); return c;
}
std::string config_bytes(const Config& c) { Encoder e; put_config(e,c); return e.data(); }
bool same_route(const Config& a, const Config& b) {
  return a.platform==b.platform && a.server==b.server && a.source==b.source && a.target==b.target;
}
void put_trade(Encoder& e, const Trade& t) {
  e.text(t.id); e.i64(t.closed_at); e.real(t.profit); e.real(t.commission);
  e.real(t.swap); e.real(t.fee); e.u64(t.currency_digits); e.text(t.payload); e.text(t.exclusion);
}
Trade get_trade(Decoder& d) {
  Trade t; t.id=d.text(); t.closed_at=d.i64(); t.profit=d.real(); t.commission=d.real();
  t.swap=d.real(); t.fee=d.real(); auto digits=d.u64();
  if(digits>8) fail("invalid currency precision"); t.currency_digits=static_cast<unsigned>(digits);
  t.payload=d.text(); t.exclusion=d.text(); return t;
}
std::string trade_bytes(const Trade& t) { Encoder e; put_trade(e,t); return e.data(); }
std::string job_bytes(const Job& j) {
  Encoder e; e.text("job-1"); e.text(j.key); e.u64(j.tag); put_trade(e,j.trade);
  e.real(j.net); e.real(j.probability); e.real(j.draw);
  e.u64(j.admitted); e.u64(j.chosen); e.u64(j.source_changed);
  e.u64(static_cast<unsigned>(j.state)); e.i64(j.decided_at); e.text(j.reason);
  e.u64(j.progress.target_ids.size()); for(auto id:j.progress.target_ids) e.u64(id);
  e.text(j.progress.data); return e.data();
}
Job get_job(Decoder& d) {
  Job j; j.key=d.text(); j.tag=d.u64(); j.trade=get_trade(d); j.net=d.real();
  j.probability=d.real(); j.draw=d.real(); j.admitted=boolean(d); j.chosen=boolean(d);
  j.source_changed=boolean(d); auto state=d.u64();
  if(state>static_cast<unsigned>(State::review)) fail("invalid job state");
  j.state=static_cast<State>(state); j.decided_at=d.i64(); j.reason=d.text();
  auto n=d.u64(); if(n>100000) fail("too many target IDs");
  for(std::uint64_t i=0;i<n;++i) j.progress.target_ids.push_back(d.u64());
  j.progress.data=d.text(); d.finish();
  if(j.tag==0 || j.probability<0 || j.probability>1 || j.draw<0 || j.draw>=1)
    fail("invalid persisted decision");
  return j;
}
std::string key_for(const Config& c,const Trade& t) {
  Encoder e; e.text(c.platform); e.text(c.server); e.u64(c.source); e.text(t.id); e.u64(c.target);
  return e.data();
}
Progress merge_progress(const Progress& saved,const Progress& next) {
  Progress out=saved;
  if(!next.data.empty()) out.data=next.data;
  for(auto id:next.target_ids) if(!id) fail("zero target receipt");
  const auto common=std::min(saved.target_ids.size(),next.target_ids.size());
  if(!std::equal(saved.target_ids.begin(),saved.target_ids.begin()+common,next.target_ids.begin()))
    fail("target receipt identity changed");
  // Positional receipts may contain equal numeric tickets from distinct MT5
  // order/deal namespaces. Preserve their order and multiplicity.
  if(next.target_ids.size()>saved.target_ids.size()) out.target_ids=next.target_ids;
  return out;
}
std::string cell(std::string s) {
  for(char& c:s) if(static_cast<unsigned char>(c)<32 || c==127) c=' ';
  if(!s.empty() && (s[0]=='=' || s[0]=='+' || s[0]=='-' || s[0]=='@')) s.insert(s.begin(),'\'');
  return s;
}

// Each frame is one durable logical event. A torn or corrupt frame refuses
// startup instead of truncating away evidence of a possibly submitted write.
class Journal {
public:
  explicit Journal(const std::string& path) {
    if(path.empty()) fail("journal path is empty");
#ifdef _WIN32
    file_=CreateFileW(std::filesystem::u8path(path).c_str(),GENERIC_READ|GENERIC_WRITE,
                      FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file_==INVALID_HANDLE_VALUE) fail("cannot exclusively open journal: "+std::to_string(GetLastError()));
#else
    file_=::open(path.c_str(),O_RDWR|O_CREAT|O_CLOEXEC,0600);
    if(file_<0) fail("cannot open journal: "+std::to_string(errno));
    if(flock(file_,LOCK_EX|LOCK_NB)!=0) { close(file_); file_=-1; fail("journal already in use"); }
    // Persist the new directory entry before any external trading side effect.
    auto parent=std::filesystem::u8path(path).parent_path();
    if(parent.empty()) parent=".";
    int dir=::open(parent.c_str(),O_RDONLY|O_DIRECTORY);
    if(dir<0) { close(file_); file_=-1; fail("cannot open journal directory"); }
    const int status=fsync(dir); close(dir);
    if(status!=0) { close(file_); file_=-1; fail("cannot sync journal directory"); }
#endif
  }
  ~Journal() {
#ifdef _WIN32
    if(file_!=INVALID_HANDLE_VALUE) CloseHandle(file_);
#else
    if(file_>=0) close(file_);
#endif
  }
  std::vector<std::string> read() {
    std::string all; char buffer[16384];
    for(;;) {
#ifdef _WIN32
      DWORD got=0; if(!ReadFile(file_,buffer,sizeof(buffer),&got,nullptr)) fail("journal read failed");
#else
      auto got=::read(file_,buffer,sizeof(buffer));
      if(got<0) { if(errno==EINTR) continue; fail("journal read failed"); }
#endif
      if(!got) break;
      all.append(buffer,static_cast<std::size_t>(got));
      if(all.size()>max_journal) fail("journal exceeds 512 MiB limit");
    }
    size_=all.size(); std::vector<std::string> frames; std::size_t pos=0;
    while(pos<all.size()) {
      const auto end=all.find('\n',pos);
      if(end==std::string::npos || end-pos>80) fail("torn journal header; manual reconciliation required");
      const auto header=all.substr(pos,end-pos); const auto sep=header.find(' ',5);
      if(header.rfind("CLA1 ",0)!=0 || sep==std::string::npos) fail("invalid journal frame");
      auto len=number<std::uint64_t>(header.substr(5,sep-5));
      auto hash=number<std::uint64_t>(header.substr(sep+1));
      pos=end+1;
      if(len>max_frame || len>=all.size()-pos) fail("torn or oversized journal frame");
      std::string body=all.substr(pos,static_cast<std::size_t>(len));
      if(all[pos+len]!='\n' || checksum(body)!=hash) fail("journal checksum mismatch");
      frames.push_back(std::move(body)); pos+=static_cast<std::size_t>(len)+1;
    }
    return frames;
  }
  void append(const std::string& data) {
    if(poisoned_) fail("journal previously failed; restart and reconcile");
    if(data.size()>max_frame) fail("journal frame exceeds 8 MiB");
    auto frame="CLA1 "+std::to_string(data.size())+" "+std::to_string(checksum(data))+"\n"+data+"\n";
    if(size_+frame.size()>max_journal) fail("journal capacity reached; preserve ledger and reconcile");
    std::size_t pos=0;
    try {
      while(pos<frame.size()) {
#ifdef _WIN32
        DWORD count=0;
        if(!WriteFile(file_,frame.data()+pos,static_cast<DWORD>(frame.size()-pos),&count,nullptr) || !count)
          fail("journal write failed");
#else
        auto count=::write(file_,frame.data()+pos,frame.size()-pos);
        if(count<0 && errno==EINTR) continue;
        if(count<=0) fail("journal write failed");
#endif
        pos+=static_cast<std::size_t>(count);
      }
#ifdef _WIN32
      if(!FlushFileBuffers(file_)) fail("journal flush failed");
#else
      if(fsync(file_)!=0) fail("journal sync failed");
#ifdef __APPLE__
      if(fcntl(file_,F_FULLFSYNC)!=0) fail("journal full sync failed");
#endif
#endif
      size_+=frame.size();
    } catch(...) { poisoned_=true; throw; }
  }
private:
  std::size_t size_=0; bool poisoned_=false;
#ifdef _WIN32
  HANDLE file_=INVALID_HANDLE_VALUE;
#else
  int file_=-1;
#endif
};
} // namespace

void Encoder::text(const std::string& v) {
  if(v.size()>max_frame) fail("encoded value too large");
  data_+=std::to_string(v.size()); data_+=':'; data_+=v;
}
void Encoder::u64(std::uint64_t v) { text(std::to_string(v)); }
void Encoder::i64(std::int64_t v) { text(std::to_string(v)); }
void Encoder::real(double v) {
  if(!std::isfinite(v)) fail("nonfinite number");
  std::ostringstream s; s.imbue(std::locale::classic());
  s<<std::setprecision(std::numeric_limits<double>::max_digits10)<<v; text(s.str());
}
std::string Decoder::text() {
  const auto end=data_.find(':',offset_);
  if(end==std::string::npos || end-offset_>10) fail("invalid string length");
  const auto n=number<std::uint64_t>(data_.substr(offset_,end-offset_));
  offset_=end+1;
  if(n>max_frame || n>data_.size()-offset_) fail("truncated encoded string");
  std::string v=data_.substr(offset_,static_cast<std::size_t>(n)); offset_+=static_cast<std::size_t>(n); return v;
}
std::uint64_t Decoder::u64() { return number<std::uint64_t>(text()); }
std::int64_t Decoder::i64() { return number<std::int64_t>(text()); }
double Decoder::real() {
  const auto value=text(); std::istringstream s(value); s.imbue(std::locale::classic());
  s>>std::noskipws; double v=0; s>>v;
  if(!s || s.peek()!=std::char_traits<char>::eof() || !std::isfinite(v)) fail("invalid real encoding");
  return v;
}
void Decoder::finish() const { if(offset_!=data_.size()) fail("unexpected encoded data"); }

void validate(const Config& c) {
  if(c.platform!="mt4" && c.platform!="mt5") fail("platform must be mt4 or mt5");
  if(c.server.empty() || c.server.size()>1024 || c.server.find('\0')!=std::string::npos) fail("invalid server identity");
  if(!c.source || !c.target || c.source==c.target) fail("source and target must be distinct nonzero logins");
  if(c.platform=="mt4" && (c.source>2147483647ULL || c.target>2147483647ULL)) fail("MT4 login out of range");
  for(double p:{c.loss_probability,c.profit_probability})
    if(!std::isfinite(p) || p<0 || p>1) fail("probability must be in [0,1]");
  if(c.enabled_since<0) fail("negative enable boundary");
}
double net_profit(const Trade& t) {
  if(t.currency_digits>8) fail("currency precision exceeds 8");
  long double total=0;
  for(double v:{t.profit,t.commission,t.swap,t.fee}) {
    if(!std::isfinite(v)) fail("nonfinite trade amount"); total+=v;
  }
  const long double factor=std::pow(10.0L,static_cast<int>(t.currency_digits));
  if(std::abs(total)*factor>9007199254740991.0L) fail("trade amount exceeds exact currency range");
  return static_cast<double>(std::round(total*factor)/factor);
}
const char* state_name(State s) {
  switch(s) {
    case State::skipped:return "skipped"; case State::selected:return "selected";
    case State::writing:return "writing"; case State::completed:return "completed";
    case State::review:return "review";
  }
  return "invalid";
}

struct Engine::Impl {
  Journal journal; Config cfg; Random random; std::uint64_t seed=0;
  std::int64_t paused_since=0,last_config_time=0;
  std::vector<std::pair<std::int64_t,std::int64_t>> pauses;
  std::map<std::string,Job> jobs;
  // Scheduling cursors affect fairness only, never decisions or recovery.
  // Keep them transient so journal schema and existing ledgers stay unchanged.
  std::string last_submit,last_reconcile;
  bool failed=false;
  Impl(const std::string& p,Config c,std::int64_t now,Random rng)
    :journal(p),cfg(std::move(c)),random(std::move(rng)) {
    validate(cfg); if(now<=0) fail("invalid current time");
    if(!random) random=[] { std::random_device r; return (static_cast<std::uint64_t>(r())<<32)^r(); };
    auto frames=journal.read(); bool initialized=false;
    Config requested=cfg;
    for(const auto& frame:frames) {
      Decoder d(frame); auto type=d.text();
      if(type=="config-1") {
        auto next=get_config(d);
        if(initialized && (!same_route(cfg,next) || cfg.enabled_since!=next.enabled_since)) fail("journal route changed");
        cfg=next; seed=d.u64(); paused_since=d.i64(); last_config_time=d.i64();
        const auto n=d.u64(); if(n>100000) fail("too many pause intervals"); pauses.clear();
        for(std::uint64_t i=0;i<n;++i) { auto a=d.i64(),b=d.i64(); if(a<=0 || b<a) fail("invalid pause interval"); pauses.emplace_back(a,b); }
        d.finish(); initialized=true;
      } else if(type=="job-1" && initialized) {
        auto j=get_job(d); if(j.key!=key_for(cfg,j.trade)) fail("journal job identity mismatch");
        jobs[j.key]=std::move(j);
      } else fail("unknown journal schema or missing config");
    }
    if(!initialized) {
      if(cfg.enabled_since==0) cfg.enabled_since=now;
      if(cfg.enabled_since>now) fail("enable boundary is in future");
      seed=random(); last_config_time=now; if(!cfg.enabled) paused_since=now;
      save_config();
    } else configure(std::move(requested),now);
  }
  void check() const { if(failed) fail("engine stopped after persistence failure"); }
  void append(const std::string& data) {
    check(); try { journal.append(data); } catch(...) { failed=true; throw; }
  }
  void save_config() {
    Encoder e; e.text("config-1"); put_config(e,cfg); e.u64(seed);
    e.i64(paused_since); e.i64(last_config_time); e.u64(pauses.size());
    for(auto p:pauses) { e.i64(p.first); e.i64(p.second); } append(e.data());
  }
  void configure(Config next,std::int64_t now) {
    check(); validate(next);
    if(!same_route(cfg,next)) fail("journal belongs to another route");
    if(next.enabled_since && next.enabled_since!=cfg.enabled_since) fail("cannot change persisted enable boundary");
    next.enabled_since=cfg.enabled_since;
    if(config_bytes(cfg)==config_bytes(next)) return;
    if(now<last_config_time || now<=0) fail("configuration time moved backwards");
    if(cfg.enabled && !next.enabled) paused_since=now;
    else if(!cfg.enabled && next.enabled) { pauses.emplace_back(paused_since,now); paused_since=0; }
    cfg=std::move(next); last_config_time=now; save_config();
  }
  void save(Job job) {
    const auto old=jobs.find(job.key); auto bytes=job_bytes(job);
    if(old!=jobs.end() && job_bytes(old->second)==bytes) return;
    append(bytes); jobs[job.key]=std::move(job);
  }
  bool paused(std::int64_t time) const {
    if(paused_since && time>=paused_since) return true;
    for(auto p:pauses) if(time>=p.first && time<=p.second) return true;
    return false;
  }
};

Engine::Engine(const std::string& path,Config c,std::int64_t now,Random r)
  :impl_(std::make_unique<Impl>(path,std::move(c),now,std::move(r))) {}
Engine::~Engine()=default;
const Config& Engine::config() const { return impl_->cfg; }
std::int64_t Engine::scan_from() const { return impl_->cfg.enabled_since; }
void Engine::configure(Config c,std::int64_t now) { impl_->configure(std::move(c),now); }
const std::map<std::string,Job>& Engine::jobs() const { return impl_->jobs; }

const Job* Engine::find_trade(const std::string& trade_id) const {
  Trade identity; identity.id=trade_id;
  const auto found=impl_->jobs.find(key_for(impl_->cfg,identity));
  return found==impl_->jobs.end()?nullptr:&found->second;
}

void Engine::invalidate_source(const std::string& trade_id,const std::string& reason) {
  auto& p=*impl_; p.check();
  const auto* existing=find_trade(trade_id);
  if(!existing || existing->source_changed) return;
  auto job=*existing; job.source_changed=true; job.state=State::review;
  job.reason="source history changed: "+reason;
  p.save(std::move(job));
}

void Engine::observe(const Trade& t,std::int64_t now) {
  auto& p=*impl_; p.check();
  if(t.id.empty() || t.id.size()>512 || t.closed_at<=0 || t.closed_at>now || t.payload.size()>max_frame/2)
    fail("invalid closed trade identity, timestamp or payload");
  const auto net=net_profit(t); const auto key=key_for(p.cfg,t); auto it=p.jobs.find(key);
  if(it!=p.jobs.end()) {
    if(trade_bytes(it->second.trade)!=trade_bytes(t) && !it->second.source_changed) {
      auto j=it->second; j.source_changed=true; j.state=State::review;
      j.reason="source history changed; frozen snapshot retained; no resubmission"; p.save(std::move(j));
    }
    return;
  }
  if(p.jobs.size()>=500000) fail("job capacity reached; preserve ledger and reconcile");
  Job j; j.key=key; j.trade=t; j.net=net; j.decided_at=now;
  std::uint64_t index=static_cast<std::uint64_t>(p.jobs.size())+1;
  j.tag=mix(p.seed+index);
  if(j.tag==0) j.tag=mix(p.seed+index+500001);
  j.state=State::skipped;
  if(t.closed_at<=p.cfg.enabled_since) j.reason="closed before enable boundary";
  else if(p.paused(t.closed_at)) j.reason="closed during explicit pause";
  else if(!t.exclusion.empty()) j.reason=t.exclusion;
  else if(net==0) j.reason="flat net result";
  else {
    j.admitted=true; j.probability=net<0?p.cfg.loss_probability:p.cfg.profit_probability;
    j.draw=static_cast<double>(p.random()>>11)*0x1.0p-53;
    j.chosen=j.draw<j.probability;
    j.state=j.chosen?State::selected:State::skipped;
    j.reason=j.chosen?"selected by probability":"not selected by probability";
  }
  p.save(std::move(j));
}

void Engine::pump(Port& port,PumpBudget budget) {
  auto& p=*impl_; p.check();
  const auto apply=[&p](Job job,Result result) {
    job=p.jobs.at(job.key);
    result.progress=merge_progress(job.progress,result.progress);
    // A receipt is incompatible with a claim that nothing was written.
    if(result.outcome==Outcome::not_written && !result.progress.target_ids.empty()) {
      result.outcome=Outcome::review;
      result.message="adapter reported not_written with target receipts; reconcile required";
    }
    // Positive proof of no side effect permits a fresh preflight baseline.
    // Prior checkpoints remain in the append-only audit log. Without this,
    // preflight-only evidence could permanently prevent a safe retry.
    if(result.outcome==Outcome::not_written) result.progress={};
    job.progress=std::move(result.progress); job.reason=std::move(result.message);
    switch(result.outcome) {
      case Outcome::completed:job.state=State::completed;break;
      case Outcome::not_written:job.state=State::selected;break;
      case Outcome::review:job.state=State::review;break;
    }
    p.save(std::move(job));
  };
  // Snapshot before reconciliation: jobs proven not-written in this invocation
  // cannot be submitted until a subsequent pump. Map order plus transient
  // cursors provides round-robin fairness even after a failed attempt.
  std::vector<std::string> ready,pending;
  for(const auto& item:p.jobs) {
    const auto& job=item.second;
    if(job.source_changed) continue;
    if(job.state==State::selected) ready.push_back(item.first);
    else if(job.state==State::writing || job.state==State::review) pending.push_back(item.first);
  }
  const auto rotate_after=[](std::vector<std::string>& keys,const std::string& last) {
    const auto next=std::upper_bound(keys.begin(),keys.end(),last);
    std::rotate(keys.begin(),next,keys.end());
  };
  rotate_after(ready,p.last_submit); rotate_after(pending,p.last_reconcile);
  std::size_t reconciled=0;
  for(const auto& key:pending) {
    if(reconciled>=budget.reconcile) break;
    const auto job=p.jobs.at(key);
    p.last_reconcile=key; ++reconciled;
    Result result;
    try { result=port.reconcile(job); }
    catch(const std::exception& e) { p.check(); result={Outcome::review,job.progress,std::string("reconcile exception: ")+e.what()}; }
    catch(...) { p.check(); result={Outcome::review,job.progress,"unknown reconcile exception"}; }
    apply(job,std::move(result));
  }
  // Inspect the entire route, including records that did not fit this call's
  // reconcile budget. Bounded work must never weaken balance isolation.
  for(const auto& item:p.jobs) {
    const auto& j=item.second;
    if(j.chosen && (j.state==State::writing || j.state==State::review)) return;
  }
  if(!p.cfg.enabled || !p.cfg.armed) return;
  std::size_t submitted=0;
  for(const auto& key:ready) {
    auto job=p.jobs.at(key);
    if(job.state!=State::selected || job.source_changed) continue;
    if(submitted>=budget.submit) break;
    job.state=State::writing; job.reason="write intent persisted"; p.save(job);
    p.last_submit=key; ++submitted;
    Result result;
    try {
      result=port.submit(job,[&p,key](const Progress& progress) {
        auto current=p.jobs.at(key);
        current.progress=merge_progress(current.progress,progress); p.save(std::move(current));
      });
    } catch(const std::exception& e) {
      p.check(); result={Outcome::review,p.jobs.at(key).progress,std::string("adapter exception: ")+e.what()};
    } catch(...) {
      p.check(); result={Outcome::review,p.jobs.at(key).progress,"unknown adapter exception"};
    }
    apply(job,std::move(result));
    if(p.jobs.at(key).state==State::review) return;
  }
}

Stats Engine::stats() const {
  Stats s;
  for(const auto& item:impl_->jobs) {
    const auto& j=item.second;
    if(j.admitted) {
      if(j.net<0) { ++s.loss_candidates; if(j.chosen) ++s.loss_selected; }
      else { ++s.profit_candidates; if(j.chosen) ++s.profit_selected; }
    }
    switch(j.state) {
      case State::skipped:++s.skipped;break;case State::selected:++s.selected;break;
      case State::writing:++s.writing;break;case State::completed:++s.completed;break;
      case State::review:++s.review;break;
    }
  }
  return s;
}
std::string Engine::report_tsv() const {
  std::ostringstream o; o.imbue(std::locale::classic()); o<<std::setprecision(17);
  o<<"source_trade\tclosed_at\tnet\tprobability\tdraw\tchosen\tstate\tapi_tag\ttarget_ids\treason\n";
  for(const auto& item:impl_->jobs) {
    const auto& j=item.second;
    o<<cell(j.trade.id)<<'\t'<<j.trade.closed_at<<'\t'<<j.net<<'\t'<<j.probability<<'\t'<<j.draw
     <<'\t'<<j.chosen<<'\t'<<state_name(j.state)<<'\t'<<j.tag<<'\t';
    for(std::size_t i=0;i<j.progress.target_ids.size();++i) { if(i) o<<','; o<<j.progress.target_ids[i]; }
    o<<'\t'<<cell(j.reason)<<'\n';
  }
  return o.str();
}
} // namespace closedalloc
