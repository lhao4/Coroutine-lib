#ifndef __MYCOROUTINE_SYNC_H_
#define __MYCOROUTINE_SYNC_H_

#include <mycoroutine/fiber.h>
#include <mycoroutine/scheduler.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <deque>
#include <mutex>
#include <utility>
#include <vector>

namespace mycoroutine {

namespace detail {

struct FiberWaiter
{
    FiberWaiter(std::shared_ptr<Fiber> f, Scheduler* s)
        : fiber(std::move(f)), scheduler(s)
    {
    }

    std::shared_ptr<Fiber> fiber;
    Scheduler* scheduler = nullptr;
    std::atomic<bool> notified{false};
};

inline std::shared_ptr<FiberWaiter> MakeCurrentWaiter()
{
    std::shared_ptr<Fiber> current = Fiber::GetThis();
    Scheduler* scheduler = Scheduler::GetThis();
    assert(current != nullptr);
    assert(scheduler != nullptr);
    return std::make_shared<FiberWaiter>(std::move(current), scheduler);
}

inline void NotifyWaiter(const std::shared_ptr<FiberWaiter>& waiter)
{
    if (!waiter)
    {
        return;
    }
    waiter->notified.store(true, std::memory_order_release);
    if (waiter->scheduler && waiter->fiber)
    {
        waiter->scheduler->scheduleLock(waiter->fiber);
    }
}

inline void SuspendCurrentUntilNotified(const std::shared_ptr<FiberWaiter>& waiter)
{
    assert(waiter != nullptr);
    std::shared_ptr<Fiber> current = Fiber::GetThis();
    assert(current != nullptr);
    while (!waiter->notified.load(std::memory_order_acquire))
    {
        current->yield();
    }
}

template <class Queue>
inline std::shared_ptr<FiberWaiter> PopWaiterFront(Queue& queue)
{
    if (queue.empty())
    {
        return nullptr;
    }
    std::shared_ptr<FiberWaiter> waiter = queue.front();
    queue.pop_front();
    return waiter;
}

} // namespace detail

/**
 * @brief 协程互斥锁
 * @details 竞争时挂起当前协程，不阻塞工作线程
 */
class CoMutex
{
public:
    void lock()
    {
        std::shared_ptr<Fiber> current = Fiber::GetThis();
        assert(current != nullptr);

        std::shared_ptr<detail::FiberWaiter> waiter;
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_locked)
                {
                    m_locked = true;
                    m_ownerFiberId = current->getId();
                    return;
                }

                if (!waiter)
                {
                    waiter = detail::MakeCurrentWaiter();
                }
                waiter->notified.store(false, std::memory_order_relaxed);
                m_waiters.push_back(waiter);
            }

            detail::SuspendCurrentUntilNotified(waiter);

            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_ownerFiberId == current->getId())
            {
                return;
            }
        }
    }

    bool tryLock()
    {
        std::shared_ptr<Fiber> current = Fiber::GetThis();
        assert(current != nullptr);

        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_locked)
        {
            return false;
        }

        m_locked = true;
        m_ownerFiberId = current->getId();
        return true;
    }

    void unlock()
    {
        std::shared_ptr<Fiber> current = Fiber::GetThis();
        assert(current != nullptr);

        std::shared_ptr<detail::FiberWaiter> next_waiter;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            assert(m_locked);
            assert(m_ownerFiberId == current->getId());

            next_waiter = detail::PopWaiterFront(m_waiters);
            if (!next_waiter)
            {
                m_locked = false;
                m_ownerFiberId = kNoOwner;
                return;
            }

            assert(next_waiter->fiber != nullptr);
            m_ownerFiberId = next_waiter->fiber->getId();
        }

        detail::NotifyWaiter(next_waiter);
    }

private:
    static constexpr std::uint64_t kNoOwner = static_cast<std::uint64_t>(-1);

    std::mutex m_mutex;
    bool m_locked = false;
    std::uint64_t m_ownerFiberId = kNoOwner;
    std::deque<std::shared_ptr<detail::FiberWaiter>> m_waiters;
};

/**
 * @brief 协程等待组
 * @details 类似 Go WaitGroup，用于等待一组协程任务完成
 */
class WaitGroup
{
public:
    void add(int delta)
    {
        std::vector<std::shared_ptr<detail::FiberWaiter>> to_wake;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            const std::int64_t next = m_count + static_cast<std::int64_t>(delta);
            assert(next >= 0);
            m_count = next;

            if (m_count == 0)
            {
                to_wake.reserve(m_waiters.size());
                while (!m_waiters.empty())
                {
                    to_wake.emplace_back(detail::PopWaiterFront(m_waiters));
                }
            }
        }

        for (auto& waiter : to_wake)
        {
            detail::NotifyWaiter(waiter);
        }
    }

    void done()
    {
        add(-1);
    }

    void wait()
    {
        std::shared_ptr<detail::FiberWaiter> waiter;
        while (true)
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_count == 0)
                {
                    return;
                }

                if (!waiter)
                {
                    waiter = detail::MakeCurrentWaiter();
                }
                waiter->notified.store(false, std::memory_order_relaxed);
                m_waiters.push_back(waiter);
            }

            detail::SuspendCurrentUntilNotified(waiter);
        }
    }

private:
    std::mutex m_mutex;
    std::int64_t m_count = 0;
    std::deque<std::shared_ptr<detail::FiberWaiter>> m_waiters;
};

/**
 * @brief 协程通道
 * @tparam T 通道元素类型
 * @details 支持有界缓冲；capacity=0 时表现为同步通道
 */
template <class T>
class Channel
{
public:
    explicit Channel(std::size_t capacity = 0)
        : m_capacity(capacity)
    {
    }

    bool send(const T& value)
    {
        return sendImpl(value);
    }

    bool send(T&& value)
    {
        return sendImpl(std::move(value));
    }

    bool recv(T& out)
    {
        std::shared_ptr<detail::FiberWaiter> self_waiter;
        while (true)
        {
            std::shared_ptr<detail::FiberWaiter> sender_to_wake;
            bool received = false;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (!m_buffer.empty())
                {
                    out = std::move(m_buffer.front());
                    m_buffer.pop_front();
                    received = true;
                    sender_to_wake = detail::PopWaiterFront(m_senderWaiters);
                }
                else if (m_closed)
                {
                    return false;
                }
                else
                {
                    sender_to_wake = detail::PopWaiterFront(m_senderWaiters);
                    if (!self_waiter)
                    {
                        self_waiter = detail::MakeCurrentWaiter();
                    }
                    self_waiter->notified.store(false, std::memory_order_relaxed);
                    m_receiverWaiters.push_back(self_waiter);
                }
            }

            if (sender_to_wake)
            {
                detail::NotifyWaiter(sender_to_wake);
            }
            if (received)
            {
                return true;
            }

            detail::SuspendCurrentUntilNotified(self_waiter);
        }
    }

    void close()
    {
        std::vector<std::shared_ptr<detail::FiberWaiter>> to_wake;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_closed)
            {
                return;
            }
            m_closed = true;

            to_wake.reserve(m_senderWaiters.size() + m_receiverWaiters.size());
            while (!m_senderWaiters.empty())
            {
                to_wake.emplace_back(detail::PopWaiterFront(m_senderWaiters));
            }
            while (!m_receiverWaiters.empty())
            {
                to_wake.emplace_back(detail::PopWaiterFront(m_receiverWaiters));
            }
        }

        for (auto& waiter : to_wake)
        {
            detail::NotifyWaiter(waiter);
        }
    }

    bool isClosed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_closed;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buffer.size();
    }

    std::size_t capacity() const
    {
        return m_capacity;
    }

private:
    template <class U>
    bool sendImpl(U&& value)
    {
        std::shared_ptr<detail::FiberWaiter> self_waiter;
        while (true)
        {
            std::shared_ptr<detail::FiberWaiter> receiver_to_wake;
            bool sent = false;

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_closed)
                {
                    return false;
                }

                const bool can_send = (m_capacity == 0)
                                          ? (!m_receiverWaiters.empty() && m_buffer.empty())
                                          : (m_buffer.size() < m_capacity);
                if (can_send)
                {
                    m_buffer.emplace_back(std::forward<U>(value));
                    sent = true;
                    receiver_to_wake = detail::PopWaiterFront(m_receiverWaiters);
                }
                else
                {
                    if (!self_waiter)
                    {
                        self_waiter = detail::MakeCurrentWaiter();
                    }
                    self_waiter->notified.store(false, std::memory_order_relaxed);
                    m_senderWaiters.push_back(self_waiter);
                }
            }

            if (receiver_to_wake)
            {
                detail::NotifyWaiter(receiver_to_wake);
            }
            if (sent)
            {
                return true;
            }

            detail::SuspendCurrentUntilNotified(self_waiter);
        }
    }

private:
    const std::size_t m_capacity = 0;
    bool m_closed = false;
    std::deque<T> m_buffer;

    mutable std::mutex m_mutex;
    std::deque<std::shared_ptr<detail::FiberWaiter>> m_senderWaiters;
    std::deque<std::shared_ptr<detail::FiberWaiter>> m_receiverWaiters;
};

} // namespace mycoroutine

#endif
