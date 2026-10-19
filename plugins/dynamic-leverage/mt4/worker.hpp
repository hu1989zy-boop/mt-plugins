#pragma once
#include "dynamicleverage/core.hpp"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mt4leverage {
// The production serialized worker, independent of Windows ABI. Ports and
// callbacks must outlive the worker; all SDK work runs on its single thread.
class Worker {
public:
  using Discover = std::function<std::vector<dynamicleverage::Account>()>;
  using Logger = std::function<void(const std::string&)>;
  Worker(dynamicleverage::Config config, dynamicleverage::Port& port, Discover discover, Logger logger);
  ~Worker();
  Worker(const Worker&) = delete;
  Worker& operator=(const Worker&) = delete;
  void start();
  void stop() noexcept;
  void wake(dynamicleverage::Login login = 0) noexcept;
  // RulesFile in this internal interface is UTF-8, unlike MT4's ACP parameters.
  void configure(dynamicleverage::Config config);
  // Called only by the port on the worker thread immediately before a write.
  bool write_permitted() noexcept;
private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace mt4leverage
