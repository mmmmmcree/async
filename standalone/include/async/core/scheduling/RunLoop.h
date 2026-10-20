#pragma once

#include <async/core/scheduling/Context.h>

namespace lcf::async {

LCF_ASYNC_EXPORT
class RunLoop
{
public:
    Scheduler getScheduler() noexcept { return m_context.getScheduler(); }
    void run() { m_context.run(); }
    void finish() noexcept { m_context.stop(); }
private:
    Context m_context;
};

}
