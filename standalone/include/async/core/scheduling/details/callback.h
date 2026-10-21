#pragma once

namespace lcf::async::details {

class Callback
{
    using Self = Callback;
    struct Operations
    {
        void (*m_invoke)(Self *);
        void (*m_abandon)(Self *) noexcept;
    };
public:
    template <auto Invoke, auto Abandon = nullptr>
    static Self make(void * owner_p) noexcept { return {&k_operations<Invoke, Abandon>, nullptr, owner_p}; }
    void invoke() { m_operations_p->m_invoke(this); }
    bool isOwned() const noexcept { return m_operations_p and m_operations_p->m_abandon; }
    void abandon() noexcept { m_operations_p->m_abandon(this); }
private:
    template <auto Invoke, auto Abandon>
    static inline constexpr Operations k_operations {Invoke, Abandon};
public:
    const Operations * m_operations_p = nullptr;
    Callback * m_next_p = nullptr;
    void * m_owner_p = nullptr;
};

}
