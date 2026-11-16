#pragma once

#include <async/config/macros.h>

LCF_ASYNC_EXPORT
namespace lcf::async {

class AnyExecutableRef
{
public:
    using completion_function_t = void (*)(void *) noexcept;
public:
    ~AnyExecutableRef() noexcept = default;
    AnyExecutableRef() = delete;
    AnyExecutableRef(void * state_p, completion_function_t execute_f, completion_function_t cancel_f) noexcept :
        m_state_p(state_p), m_execute_f(execute_f), m_cancel_f(cancel_f) {}
    AnyExecutableRef(const AnyExecutableRef &) noexcept = default;
    AnyExecutableRef(AnyExecutableRef &&) noexcept = default;
    AnyExecutableRef & operator=(const AnyExecutableRef &) noexcept = default;
    AnyExecutableRef & operator=(AnyExecutableRef &&) noexcept = default;
public:
    void execute() noexcept { m_execute_f(m_state_p); }
    void cancel() noexcept { m_cancel_f(m_state_p); }
private:
    void * m_state_p;
    completion_function_t m_execute_f;
    completion_function_t m_cancel_f;
};

} // namespace lcf::async
