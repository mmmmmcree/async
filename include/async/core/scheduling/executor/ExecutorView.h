#pragma once

#include <async/core/scheduling/declarations/concepts.h>

#include <memory>

LCF_ASYNC_EXPORT
namespace lcf::async {

class ExecutorView
{
public:
    ExecutorView() = delete;
    template <work_executor_c Executor>
    requires (not std::same_as<Executor, ExecutorView>)
    explicit ExecutorView(Executor & executor) noexcept :
        m_executor_p(std::addressof(executor)),
        m_execute_one_f([](void * state_p) noexcept { return static_cast<Executor *>(state_p)->executeOne(); }),
        m_is_empty_f([](const void * state_p) noexcept { return static_cast<const Executor *>(state_p)->isEmpty(); }) {}
public:
    bool executeOne() noexcept { return m_execute_one_f(m_executor_p); }
    bool isEmpty() const noexcept { return m_is_empty_f(m_executor_p); }
private:
    void * m_executor_p;
    bool (*m_execute_one_f)(void *) noexcept;
    bool (*m_is_empty_f)(const void *) noexcept;
};

} // namespace lcf::async
