#pragma once

#include <async/core/scheduling/Context.h>
#include <async/core/scheduling/Scheduler.h>
#include <asio/executor_work_guard.hpp>

namespace lcf::async {

LCF_ASYNC_EXPORT
class RunLoop
{
public:
    Scheduler getScheduler() noexcept { return Scheduler {m_context.get_executor()}; }
    void run() { m_context.run(); }
    void finish() noexcept { m_context.stop(); }
private:
    Context m_context;
    asio::executor_work_guard<Context::executor_type> m_work {m_context.get_executor()};
};

}
