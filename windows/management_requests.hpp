#pragma once

#include "tradecopier/management.hpp"
#include <optional>
#include <stdexcept>
#include <utility>

namespace tradecopier::manager {

struct Action {
    Id server=0, app=0;
    management::Request request;
    bool load_config=false;
    bool background=false;
};

struct Connection {
    bool fresh=false;
    Id server=0, app=0;
    std::string session;
    Id revision=0;
};

// Keep the blocking reason visible independently of the last operation result.
inline std::string RuntimeGuardHint(const management::Response& live,bool fresh) {
    if (!fresh) return "连接未就绪或状态已过期，请连接 / 刷新。";
    if (!live.halt.empty()) return "运行告警（不停止其他映射）："+live.halt;
    if (!live.apply_block.empty()) return live.apply_block;
    if (live.paused) return "操作员已暂停新仓；已有仓位继续管理，订阅仍可独立加入或退出。";
    return "订阅可随时加入或退出；退出会平掉该关系已有仓位，其他关系继续运行。";
}

inline bool SameTarget(const Action& action, Id server, Id app) {
    return action.server==server && action.app==app;
}

// Keep the session, revision, configuration and intent selected at click time.
// A background response must never silently rebase a user's write operation.
inline std::string DispatchError(const Action& action, const Connection& live) {
    using management::Operation;
    if (action.request.operation==Operation::Status && !action.load_config) return {};
    if (!live.fresh || !SameTarget(action,live.server,live.app))
        return "连接已中断或状态已过期，本次操作未发送；请重新连接后操作。";
    if (action.request.operation==Operation::Status) return {};
    if (action.request.session!=live.session)
        return "执行器会话已变化，本次操作未发送；请核对当前状态后重新操作。";
    if (action.request.operation!=Operation::Inspect && action.request.operation!=Operation::UpdateSubscriptions && action.request.revision!=live.revision)
        return "运行状态已变化，本次操作未发送；请核对当前状态后重新操作。";
    return {};
}

// One pipe exchange at a time. Only a background Status read can be followed
// by one deferred foreground action; extra clicks never replace that action.
class Requests {
public:
    enum class Accepted { Start, Deferred, Busy };
    Accepted Schedule(Action action) {
        if (action.background && (action.load_config || action.request.operation!=management::Operation::Status))
            throw std::logic_error("only status polling may run in the background");
        if (!active_) { active_=std::move(action); return Accepted::Start; }
        if (active_->background && !deferred_ && !action.background) {
            deferred_=std::move(action); return Accepted::Deferred;
        }
        return Accepted::Busy;
    }
    const std::optional<Action>& active() const { return active_; }
    bool busy() const { return deferred_.has_value() || (active_ && !active_->background); }
    bool loading_config() const {
        return (active_ && active_->load_config) || (deferred_ && deferred_->load_config);
    }
    std::optional<Action> Complete() {
        active_.reset();
        auto next=std::move(deferred_);
        deferred_.reset();
        return next;
    }
private:
    std::optional<Action> active_, deferred_;
};

} // namespace tradecopier::manager
