#pragma once

#include <async/core/queue/declarations/concepts.h>

LCF_ASYNC_EXPORT
namespace lcf::async::queue::intrusive {

template <linked_node_c Node>
class Queue
{
public:
    using Value = Node;
public:
    Queue() noexcept = default;
    Queue(const Queue &) = delete;
    Queue(Queue &&) = delete;
    Queue & operator=(const Queue &) = delete;
    Queue & operator=(Queue &&) = delete;
public:
    bool isEmpty() const noexcept { return not m_tail_p; }
    Node * popFront() noexcept
    {
        if (not m_tail_p) { return nullptr; }
        auto * node_p = m_tail_p->getNext();
        if (node_p == m_tail_p) { m_tail_p = nullptr; }
        else { m_tail_p->setNext(node_p->getNext()); }
        node_p->setNext(nullptr);
        return node_p;
    }
    void pushBack(Node & node) noexcept
    {
        if (m_tail_p) {
            node.setNext(m_tail_p->getNext());
            m_tail_p->setNext(&node);
        } else { node.setNext(&node); }
        m_tail_p = &node;
    }
private:
    Node * m_tail_p = nullptr;
};

} // namespace lcf::async::queue::intrusive
