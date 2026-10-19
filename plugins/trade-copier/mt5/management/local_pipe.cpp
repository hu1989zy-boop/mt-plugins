#include "local_pipe.hpp"
#include <windows.h>
#include <sddl.h>
#include <array>
#include <stdexcept>

namespace tradecopier::management {
namespace {
[[noreturn]] void Fail(const char* message) {
    throw std::runtime_error(std::string(message)+" (Windows "+std::to_string(GetLastError())+")");
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h) {}
    ~Handle() { if (value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&)=delete;
    Handle& operator=(const Handle&)=delete;
};
void VerifyServer(HANDLE pipe) {
    ULONG pid=0;
    if (!GetNamedPipeServerProcessId(pipe,&pid)) Fail("GetNamedPipeServerProcessId");
    Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid));
    if (!process.value) Fail("cannot verify management server process");
    HANDLE raw=nullptr;
    if (!OpenProcessToken(process.value,TOKEN_QUERY,&raw)) Fail("cannot verify management server token");
    Handle token(raw);
    BYTE system_sid[SECURITY_MAX_SID_SIZE]{},admin_sid[SECURITY_MAX_SID_SIZE]{};
    DWORD system_size=sizeof(system_sid),admin_size=sizeof(admin_sid);
    if (!CreateWellKnownSid(WinLocalSystemSid,nullptr,system_sid,&system_size) ||
        !CreateWellKnownSid(WinBuiltinAdministratorsSid,nullptr,admin_sid,&admin_size)) Fail("CreateWellKnownSid");
    DWORD size=0;
    GetTokenInformation(token.value,TokenUser,nullptr,0,&size);
    std::vector<BYTE> user(size);
    if (!size || !GetTokenInformation(token.value,TokenUser,user.data(),size,&size)) Fail("TokenUser");
    if (EqualSid(reinterpret_cast<TOKEN_USER*>(user.data())->User.Sid,system_sid)) return;
    GetTokenInformation(token.value,TokenGroups,nullptr,0,&size);
    std::vector<BYTE> groups(size);
    if (!size || !GetTokenInformation(token.value,TokenGroups,groups.data(),size,&size)) Fail("TokenGroups");
    const auto* list=reinterpret_cast<TOKEN_GROUPS*>(groups.data());
    for (DWORD i=0;i<list->GroupCount;++i)
        if ((list->Groups[i].Attributes&SE_GROUP_ENABLED) && EqualSid(list->Groups[i].Sid,admin_sid)) return;
    throw std::runtime_error("拒绝连接：管道服务端不是 System 或已提升权限的管理员进程");
}
std::array<char,8> Header(std::size_t size) {
    if (!size || size>kMaxMessage) throw std::runtime_error("invalid pipe message length");
    std::array<char,8> bytes{'T','C','M','1',0,0,0,0};
    for (unsigned i=0;i<4;++i) bytes[4+i]=static_cast<char>((size>>(8*i))&255);
    return bytes;
}
std::size_t Length(const std::array<char,8>& bytes) {
    if (bytes[0]!='T'||bytes[1]!='C'||bytes[2]!='M'||bytes[3]!='1') throw std::runtime_error("wrong pipe protocol");
    std::size_t size=0;
    for (unsigned i=0;i<4;++i) size|=std::size_t(static_cast<unsigned char>(bytes[4+i]))<<(8*i);
    if (!size || size>kMaxMessage) throw std::runtime_error("invalid pipe message length");
    return size;
}
void Transfer(HANDLE file, bool write, char* data, std::size_t size, ULONGLONG deadline) {
    Handle event(CreateEventW(nullptr,TRUE,FALSE,nullptr));
    if (!event.value) Fail("CreateEvent");
    std::size_t offset=0;
    while (offset<size) {
        OVERLAPPED ov{}; ov.hEvent=event.value; ResetEvent(event.value);
        DWORD count=0;
        BOOL done=write?WriteFile(file,data+offset,static_cast<DWORD>(size-offset),&count,&ov):
                        ReadFile(file,data+offset,static_cast<DWORD>(size-offset),&count,&ov);
        if (!done) {
            if (GetLastError()!=ERROR_IO_PENDING) Fail("pipe transfer");
            const auto now=GetTickCount64();
            const DWORD wait=now>=deadline?0:static_cast<DWORD>(deadline-now);
            if (WaitForSingleObject(event.value,wait)!=WAIT_OBJECT_0) {
                CancelIoEx(file,&ov);
                GetOverlappedResult(file,&ov,&count,TRUE); // drain before stack buffer dies
                throw std::runtime_error("连接超时；命令结果未知，请刷新核对，不会自动重发");
            }
            if (!GetOverlappedResult(file,&ov,&count,FALSE)) Fail("pipe completion");
        }
        if (!count) throw std::runtime_error("pipe disconnected");
        offset+=count;
    }
}
}
std::wstring PipeName(Id server, Id app) {
    if (!server || !app || app>4095) throw std::runtime_error("invalid server/application id");
    return L"\\\\.\\pipe\\TradeCopier.Management.v1."+std::to_wstring(server)+L"."+std::to_wstring(app);
}
struct PipeServer::Impl {
    Handle pipe, event;
    OVERLAPPED ov{};
    enum class Step { Connect, Header, Body, AwaitReply, Write, Ack } step=Step::Connect;
    bool pending=false, completed=false;
    DWORD bytes=0;
    ULONGLONG since=0;
    std::array<char,8> header{};
    std::string body, output;
    std::size_t offset=0;
    Impl(Id server,Id app):event(CreateEventW(nullptr,TRUE,FALSE,nullptr)) {
        if (!event.value) Fail("CreateEvent");
        PSECURITY_DESCRIPTOR descriptor=nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:P(D;;GA;;;NU)(A;;GA;;;SY)(A;;GA;;;BA)",SDDL_REVISION_1,&descriptor,nullptr)) Fail("pipe ACL");
        SECURITY_ATTRIBUTES security{sizeof(security),descriptor,FALSE};
        pipe.value=CreateNamedPipeW(PipeName(server,app).c_str(),
            PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,
            1,65536,65536,0,&security);
        const DWORD error=GetLastError(); LocalFree(descriptor);
        if (pipe.value==INVALID_HANDLE_VALUE) { SetLastError(error); Fail("CreateNamedPipe"); }
        Begin();
    }
    ~Impl() { Cancel(); }
    void Cancel() {
        if (pending) { CancelIoEx(pipe.value,&ov); GetOverlappedResult(pipe.value,&ov,&bytes,TRUE); pending=false; }
    }
    void Reset() {
        Cancel(); DisconnectNamedPipe(pipe.value);
        step=Step::Connect; body.clear(); output.clear(); offset=0; Begin();
    }
    void Begin() {
        ov={}; ov.hEvent=event.value; ResetEvent(event.value); bytes=0; completed=false;
        BOOL done=FALSE;
        if (step==Step::Connect) {
            done=ConnectNamedPipe(pipe.value,&ov);
            if (!done && GetLastError()==ERROR_PIPE_CONNECTED) done=TRUE;
        } else if ((step==Step::Header || step==Step::Ack)) {
            done=ReadFile(pipe.value,header.data()+offset,static_cast<DWORD>(header.size()-offset),&bytes,&ov);
        } else if (step==Step::Body) {
            done=ReadFile(pipe.value,body.data()+offset,static_cast<DWORD>(body.size()-offset),&bytes,&ov);
        } else if (step==Step::Write) {
            done=WriteFile(pipe.value,output.data()+offset,static_cast<DWORD>(output.size()-offset),&bytes,&ov);
        } else return;
        since=GetTickCount64();
        pending=!done && GetLastError()==ERROR_IO_PENDING;
        completed=done!=FALSE;
        if (!done && !pending) throw std::runtime_error("pipe peer disconnected");
    }
    std::optional<std::string> Poll() {
        try {
            // A finite work budget prevents a pipe client starving trade processing.
            for (unsigned n=0;n<8;++n) {
                if (step==Step::AwaitReply) return {};
                if (pending) {
                    if (WaitForSingleObject(event.value,0)!=WAIT_OBJECT_0) {
                        if (step!=Step::Connect && GetTickCount64()-since>3000) Reset();
                        return {};
                    }
                    pending=false;
                    if (!GetOverlappedResult(pipe.value,&ov,&bytes,FALSE)) { Reset(); return {}; }
                    completed=true;
                }
                if (!completed) return {};
                if (step==Step::Connect) { step=Step::Header; offset=0; Begin(); continue; }
                if (!bytes) { Reset(); return {}; }
                offset+=bytes;
                const auto wanted=(step==Step::Header || step==Step::Ack)?header.size():(step==Step::Body?body.size():output.size());
                if (offset<wanted) { Begin(); continue; }
                offset=0;
                if (step==Step::Header) { body.assign(Length(header),'\0'); step=Step::Body; Begin(); }
                else if (step==Step::Body) { step=Step::AwaitReply; return body; }
                else if (step==Step::Write) { step=Step::Ack; Begin(); }
                else { Reset(); return {}; }
            }
        } catch (...) { Reset(); }
        return {};
    }
};
PipeServer::PipeServer(Id server,Id app):impl_(std::make_unique<Impl>(server,app)) {}
PipeServer::~PipeServer()=default;
std::optional<std::string> PipeServer::Poll() { return impl_->Poll(); }
void PipeServer::Reply(const std::string& data) {
    auto& p=*impl_;
    if (p.step!=Impl::Step::AwaitReply) return;
    const auto header=Header(data.size());
    p.output.assign(header.data(),header.size()); p.output+=data; p.offset=0; p.step=Impl::Step::Write;
    try { p.Begin(); } catch (...) { p.Reset(); }
}
Response Exchange(Id server,Id app,const Request& request) {
    const auto name=PipeName(server,app);
    const auto deadline=GetTickCount64()+8000;
    HANDLE raw=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
    if (raw==INVALID_HANDLE_VALUE && GetLastError()==ERROR_PIPE_BUSY) {
        if (!WaitNamedPipeW(name.c_str(),1000)) Fail("管理通道忙碌");
        raw=CreateFileW(name.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
    }
    Handle file(raw);
    if (file.value==INVALID_HANDLE_VALUE) Fail("无法连接本机执行器；请检查 ManagementEnabled=1，并以管理员身份运行界面");
    VerifyServer(file.value);
    auto payload=EncodeRequest(request); auto header=Header(payload.size());
    Transfer(file.value,true,header.data(),header.size(),deadline);
    Transfer(file.value,true,payload.data(),payload.size(),deadline);
    Transfer(file.value,false,header.data(),header.size(),deadline);
    payload.assign(Length(header),'\0'); Transfer(file.value,false,payload.data(),payload.size(),deadline);
    // Acknowledge consumption before the server disconnects its pipe buffer.
    Transfer(file.value,true,header.data(),header.size(),deadline);
    return DecodeResponse(payload);
}
} // namespace tradecopier::management
