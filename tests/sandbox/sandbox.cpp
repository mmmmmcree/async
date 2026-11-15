#include "library_under_test.h"
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <memory_resource>
#include <optional>
#include <thread>
#include <vector>
namespace async = lcf::async;
namespace ex = async::exec;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

template <async::operation_state_c Operation>
class OneShotTimer
{
public:
    ~OneShotTimer() noexcept = default;
    OneShotTimer() = delete;
    explicit OneShotTimer(Operation & operation) noexcept : m_operation_p(&operation) {}
    OneShotTimer(const OneShotTimer &) = delete;
    OneShotTimer(OneShotTimer &&) = delete;
    OneShotTimer & operator=(const OneShotTimer &) = delete;
    OneShotTimer & operator=(OneShotTimer &&) = delete;
    std::error_code start(async::ServiceHandle handle) noexcept
    { m_handle = handle; handle.setWakeTime(Clock::now() + 20ms); return {}; }
    async::DriveReport driveOne() noexcept
    { ex::start(*m_operation_p); m_handle->notifyStopped(); return {.m_has_progress = true}; }
    void requestStop() noexcept { m_handle->notifyStopped(); }
private:
    Operation * m_operation_p;
    std::optional<async::ServiceHandle> m_handle;
};
struct Outcome
{
    int m_value = 0;
    std::thread::id m_left_thread, m_right_thread, m_receiver_thread;
    Clock::time_point m_left_start, m_left_finish, m_right_start, m_right_finish;
    std::atomic<bool> m_completed {false};
};
class Receiver
{
public:
    using receiver_concept = async::protocols::receiver_t;
    ~Receiver() noexcept = default;
    Receiver() = delete;
    explicit Receiver(Outcome & outcome) noexcept : m_outcome_p(&outcome) {}
    Receiver(const Receiver &) noexcept = default;
    Receiver(Receiver &&) noexcept = default;
    Receiver & operator=(const Receiver &) noexcept = default;
    Receiver & operator=(Receiver &&) noexcept = default;
    void set_value(int value) && noexcept
    {
        m_outcome_p->m_value = value;
        m_outcome_p->m_receiver_thread = std::this_thread::get_id();
        m_outcome_p->m_completed.store(true, std::memory_order_release);
    }
    template <typename Error> void set_error(Error &&) && noexcept { this->finish(); }
    void set_stopped() && noexcept { this->finish(); }
private:
    void finish() noexcept { m_outcome_p->m_completed.store(true, std::memory_order_release); }
    Outcome * m_outcome_p;
};
int main()
{
    std::array<std::byte, 4096> configuration_bytes;
    std::pmr::monotonic_buffer_resource configuration {configuration_bytes.data(), configuration_bytes.size(), std::pmr::null_memory_resource()};
    async::ContextInfo info {configuration};
    info.setWorkerCount(2).setDispatcherCount(2).setServiceCapacity(1);
    const auto caller = info.addPort(async::Priority::eHigh);
    const auto shared = info.addSharedScheduler();
    const auto [runtime_size, runtime_alignment, worker_size, worker_alignment] = info.getStorageRequirements<>();
    std::vector<std::byte> runtime_bytes(runtime_size + runtime_alignment - 1), worker_bytes(worker_size + worker_alignment - 1);
    std::pmr::monotonic_buffer_resource runtime {runtime_bytes.data(), runtime_bytes.size(), std::pmr::null_memory_resource()};
    std::pmr::monotonic_buffer_resource workers {worker_bytes.data(), worker_bytes.size(), std::pmr::null_memory_resource()};
    info.setMemoryResources(runtime, workers);
    async::Context context;
    if (const std::error_code ec = context.create(std::move(info))) { std::cerr << ec.message(); return 1; }
    const auto port = context.getSubmissionPort(caller);
    auto shared_scheduler = port.getSchedulerHandle(shared);
    shared_scheduler.setPriority(async::Priority::eNormal);
    const auto caller_scheduler = port.getPortSchedulerHandle();
    Outcome outcome;
    const auto caller_thread = std::this_thread::get_id();
    const auto branch = [shared_scheduler](int value, std::thread::id & thread, Clock::time_point & start, Clock::time_point & finish) {
        return ex::schedule(shared_scheduler) | ex::then([value, &thread, &start, &finish]() noexcept {
            thread = std::this_thread::get_id(); start = Clock::now();
            std::this_thread::sleep_for(100ms); finish = Clock::now(); return value;
        });
    };
    auto graph = ex::when_all(branch(20, outcome.m_left_thread, outcome.m_left_start, outcome.m_left_finish),
        branch(22, outcome.m_right_thread, outcome.m_right_start, outcome.m_right_finish))
        | ex::then([](int left, int right) noexcept { return left + right; }) | ex::continues_on(caller_scheduler);
    auto operation = ex::connect(std::move(graph), Receiver {outcome});
    OneShotTimer service {operation};
    if (const std::error_code ec = context.registerService(service)) { std::cerr << ec.message(); return 1; }
    std::error_code run_ec;
    std::thread driver {[&] { run_ec = context.run(); }};
    auto execution_port = context.getExecutionPort(caller);
    while (not outcome.m_completed.load(std::memory_order_acquire)) {
        if (const auto report = execution_port.waitAndExecuteOne(); report.isStopped()) { break; }
    }
    context.closeSubmissions();
    driver.join();
    const bool parallel = outcome.m_left_start < outcome.m_right_finish and outcome.m_right_start < outcome.m_left_finish;
    const bool on_caller = outcome.m_receiver_thread == caller_thread;
    std::cout << "result: " << outcome.m_value << ", branches overlap: " << parallel
        << ", receiver on caller thread: " << on_caller << '\n';
    return not run_ec and outcome.m_value == 42 and parallel and on_caller ? 0 : 1;
}
