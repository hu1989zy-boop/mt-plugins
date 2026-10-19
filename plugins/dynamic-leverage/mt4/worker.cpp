#include "worker.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace mt4leverage {
using dynamicleverage::Config;
struct Worker::State {
public:
  State(Config config, dynamicleverage::Port& port, Discover discover, Logger logger)
    : port_(port), discover_(std::move(discover)), logger_(std::move(logger)), config_(std::move(config)) {}
  ~State() { stop(); }

  void start() {
    if (started_.exchange(true)) throw std::runtime_error("MT4 worker has already started");
    thread_ = std::thread([this] { run(); });
  }
  void stop() noexcept {
    stopping_.store(true);
    wake(0);
    if (thread_.joinable()) {
      try { thread_.join(); } catch (...) { std::terminate(); }
    }
  }
  void wake(dynamicleverage::Login login) noexcept {
    try {
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (login) pending_.insert(login);
        else full_ = true;
      }
      changed_.notify_one();
    } catch (...) {}
  }
  void configure(Config next) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (faulted_.load() || stopping_.load()) throw std::runtime_error("MT4 worker is stopped; reload the plugin");
      config_ = std::move(next);
      generation_.fetch_add(1);
      reload_ = full_ = true;
    }
    changed_.notify_one();
  }

  bool write_permitted() noexcept {
    if (stopping_.load() || generation_.load() != running_generation_) return false;
    try {
      if (!path_.empty() && dynamicleverage::rules_json(dynamicleverage::load_rules_file(path_, "mt4")) != signature_)
        return false;
    } catch (...) { return false; }
    return !stopping_.load() && generation_.load() == running_generation_;
  }

private:
  void log(const std::string& message) noexcept {
    try { if (logger_) logger_(message); } catch (...) {}
  }
  void run() noexcept {
    try {
      using Clock = std::chrono::steady_clock;
      std::map<dynamicleverage::Login, std::string> errors;
      std::string config_error;
      std::vector<dynamicleverage::Login> candidates;
      Config config;
      bool valid = false;
      std::uint64_t applied_generation = 0;
      auto deadline = Clock::now();
      while (!stopping_.load()) {
        Config sdk;
        std::vector<dynamicleverage::Login> pending;
        bool full = false, refresh = false;
        {
          std::unique_lock<std::mutex> lock(mutex_);
          changed_.wait_until(lock, deadline, [this] { return full_ || reload_ || !pending_.empty() || stopping_.load(); });
          if (stopping_.load()) break;
          refresh = reload_ || Clock::now() >= deadline;
          full = full_ || refresh;
          sdk = config_;
          running_generation_ = generation_.load();
          pending.assign(pending_.begin(), pending_.end());
          pending_.clear(); full_ = reload_ = false;
        }
        if (refresh) {
          try {
            auto next = sdk;
            const auto next_path = sdk.rules_file;
            std::string next_signature;
            if (!next_path.empty()) {
              next = dynamicleverage::load_rules_file(next_path, "mt4");
              next_signature = dynamicleverage::rules_json(next);
              next.enabled = sdk.enabled && next.enabled;
            }
            // Discovery happens on the polling/configuration boundary only.
            // Trade and leverage-update callbacks process individual accounts.
            auto discovered = next.enabled && dynamicleverage::has_group_rules(next)
              ? discover_() : std::vector<dynamicleverage::Account>{};
            candidates = next.enabled ? dynamicleverage::candidate_accounts(next, discovered)
                                      : std::vector<dynamicleverage::Login>{};
            const bool configuration_changed = !valid || signature_ != next_signature ||
              path_ != next_path || applied_generation != running_generation_;
            if (configuration_changed) errors.clear();
            config = std::move(next);
            path_ = next_path;
            signature_ = std::move(next_signature);
            valid = true;
            config_error.clear();
            applied_generation = running_generation_;
            if (configuration_changed)
              log("MT4 configuration loaded: enabled=" + std::to_string(config.enabled ? 1 : 0) +
                  " rules=" + std::to_string(config.rules.size()) +
                  " candidate_accounts=" + std::to_string(candidates.size()) +
                  " poll_ms=" + std::to_string(config.poll_interval_ms));
          } catch (const std::exception& error) {
            valid = false; candidates.clear();
            if (config_error != error.what()) {
              config_error = error.what();
              log("Dynamic Leverage suspended: " + config_error);
            }
          } catch (...) {
            valid = false; candidates.clear();
            const std::string message = "unknown configuration/discovery error";
            if (config_error != message) { config_error = message; log("Dynamic Leverage suspended: " + message); }
          }
          deadline = Clock::now() + std::chrono::milliseconds(valid ? config.poll_interval_ms : 1000);
        }
        if (!valid || !config.enabled) continue;
        auto accounts = full ? candidates : pending;
        if (full && !pending.empty()) {
          // Events captured at the polling boundary may reference an account
          // missing from the discovery snapshot (new account or group move).
          // Preserve them instead of postponing reconciliation a full interval.
          accounts.insert(accounts.end(), pending.begin(), pending.end());
          std::sort(accounts.begin(), accounts.end());
          accounts.erase(std::unique(accounts.begin(), accounts.end()), accounts.end());
        }
        for (const auto login : accounts) {
          if (stopping_.load() || generation_.load() != running_generation_) break;
          try {
            const auto result = dynamicleverage::reconcile(config, login, port_);
            errors.erase(login);
            if (result.status == dynamicleverage::Status::updated)
              log("account=" + std::to_string(login) + " rule=" + result.rule_id +
                  " lots=" + dynamicleverage::format_lots(result.volume) +
                  " leverage=" + std::to_string(result.previous) + "->" + std::to_string(result.target));
          } catch (const std::exception& error) {
            if (errors[login] != error.what()) {
              errors[login] = error.what();
              log("account=" + std::to_string(login) + " reconciliation failed: " + error.what());
            }
          } catch (...) {
            const std::string message = "unknown SDK failure; account will be retried";
            if (errors[login] != message) { errors[login] = message; log("account=" + std::to_string(login) + " " + message); }
          }
        }
      }
    } catch (const std::exception& error) {
      faulted_.store(true);
      log(std::string("MT4 dynamic leverage worker stopped: ") + error.what());
    } catch (...) { faulted_.store(true); log("MT4 dynamic leverage worker stopped after an unknown failure"); }
  }

  dynamicleverage::Port& port_;
  Discover discover_;
  Logger logger_;
  // The worker thread is the only caller of write_permitted(), through Port.
  std::string path_, signature_;
  std::uint64_t running_generation_ = 0;
  std::atomic<bool> started_{false};
  std::mutex mutex_;
  std::condition_variable changed_;
  Config config_;
  std::set<dynamicleverage::Login> pending_;
  bool full_ = true, reload_ = true;
  std::atomic<bool> stopping_{false};
  std::atomic<bool> faulted_{false};
  std::atomic<std::uint64_t> generation_{0};
  std::thread thread_;
};

Worker::Worker(Config config, dynamicleverage::Port& port, Discover discover, Logger logger)
  : state_(std::make_unique<State>(std::move(config), port, std::move(discover), std::move(logger))) {}
Worker::~Worker() = default;
void Worker::start() { state_->start(); }
void Worker::stop() noexcept { state_->stop(); }
void Worker::wake(dynamicleverage::Login login) noexcept { state_->wake(login); }
void Worker::configure(Config config) { state_->configure(std::move(config)); }
bool Worker::write_permitted() noexcept { return state_->write_permitted(); }
} // namespace mt4leverage
