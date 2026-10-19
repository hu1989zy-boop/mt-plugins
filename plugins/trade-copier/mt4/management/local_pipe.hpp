#pragma once
#include "tradecopier/management.hpp"
#include <memory>
#include <optional>

namespace tradecopier::management {
// Windows-only, local administrators/System. No remote pipe names or TCP port.
std::wstring PipeName(Id server, Id app);
class PipeServer {
public:
    PipeServer(Id server, Id app);
    ~PipeServer();
    PipeServer(const PipeServer&)=delete;
    PipeServer& operator=(const PipeServer&)=delete;
    std::optional<std::string> Poll(); // nonblocking; invoked by execution worker
    void Reply(const std::string&);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
Response Exchange(Id server, Id app, const Request&); // bounded I/O, call off UI thread
} // namespace tradecopier::management
