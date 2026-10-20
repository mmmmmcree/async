#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace lcf::async::details {

// Extracted from ChainJunction's subscribe/publish handshake; no executor is required.
class CompletionEvent
{
public:
    struct Observer
    {
        std::function<void()> m_notify;
    };
    using ObserverPtr = std::shared_ptr<Observer>;
    using Observers = std::vector<ObserverPtr>;
public:
    bool subscribe(const ObserverPtr & observer_sp)
    {
        std::lock_guard lock {m_mutex};
        if (m_ready) { return false; }
        m_observers.push_back(observer_sp);
        return true;
    }
    void unsubscribe(const Observer * observer_p) noexcept
    {
        std::lock_guard lock {m_mutex};
        std::erase_if(m_observers, [=](const auto & entry_sp) { return entry_sp.get() == observer_p; });
    }
    template <typename Publish>
    Observers complete(Publish publish) noexcept
    {
        std::lock_guard lock {m_mutex};
        publish();
        m_ready = true;
        return std::move(m_observers);
    }
private:
    std::mutex m_mutex;
    Observers m_observers;
    bool m_ready = false;
};

}
