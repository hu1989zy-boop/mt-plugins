#include "local_pipe.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace tradecopier;
using namespace tradecopier::management;
int main() {
    SID_IDENTIFIER_AUTHORITY authority=SECURITY_NT_AUTHORITY;
    PSID admin=nullptr; BOOL member=FALSE;
    if (!AllocateAndInitializeSid(&authority,2,SECURITY_BUILTIN_DOMAIN_RID,DOMAIN_ALIAS_RID_ADMINS,0,0,0,0,0,0,&admin)) return 1;
    CheckTokenMembership(nullptr,admin,&member); FreeSid(admin);
    if (!member) { std::cout<<"SKIP: pipe integration test requires elevated administrator\n"; return 77; }
    try {
        const Id server=900000000ULL+GetCurrentProcessId();
        PipeServer pipe(server,3101);
        bool duplicate_refused=false;
        try { PipeServer duplicate(server,3101); } catch (...) { duplicate_refused=true; }
        if (!duplicate_refused) throw std::runtime_error("duplicate server accepted");
        std::atomic<bool> done{false}; std::exception_ptr client_error;
        std::thread client([&] {
            try {
                // A peer that disconnects partway through its header must not
                // terminate the server or poison the next session.
                HANDLE broken=CreateFileW(PipeName(server,3101).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
                if (broken==INVALID_HANDLE_VALUE) throw std::runtime_error("broken peer setup failed");
                DWORD written=0; WriteFile(broken,"TC",2,&written,nullptr); CloseHandle(broken);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                for (unsigned n=0;n<3;++n) {
                    Request request; request.session="test"; request.revision=UINT64_MAX;
                    const auto response=Exchange(server,3101,request);
                    if (!response.ok || response.revision!=UINT64_MAX || response.message!=std::string(200000,'x'))
                        throw std::runtime_error("pipe roundtrip was incomplete");
                }
            } catch (...) { client_error=std::current_exception(); }
            done=true;
        });
        std::exception_ptr server_error;
        try {
            while (!done) {
                if (auto data=pipe.Poll()) {
                    const auto request=DecodeRequest(*data); Response response;
                    response.revision=request.revision; response.message=std::string(200000,'x');
                    pipe.Reply(EncodeResponse(response));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        } catch (...) { server_error=std::current_exception(); }
        client.join();
        if (server_error) std::rethrow_exception(server_error);
        if (client_error) std::rethrow_exception(client_error);
        // Destruction cancels an outstanding overlapped connect/read.
        std::cout<<"management pipe tests passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
