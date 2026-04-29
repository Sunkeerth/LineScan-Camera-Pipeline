// BoundedQueue.h

#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <chrono>

/**
 * @brief Thread-safe bounded FIFO queue — the bridge between Block 1 and Block 2.
 *
 * Producer-Consumer pattern.
 * Capacity = m (column count) keeps memory bounded as required by the spec.
 */
template<typename T>
class BoundedQueue
{
public:
    explicit BoundedQueue(size_t capacity = 64) : m_capacity(capacity) {}

    BoundedQueue(const BoundedQueue&)            = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /** Push an item. Blocks if full. Returns false if queue was closed. */
    bool push(T item)
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_not_full.wait(lk, [this]{ return m_queue.size() < m_capacity || m_closed; });
        if (m_closed) return false;
        m_queue.push(std::move(item));
        m_not_empty.notify_one();
        return true;
    }

    /** Pop an item. Blocks until available or queue closed. Returns nullopt if closed+empty. */
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_not_empty.wait(lk, [this]{ return !m_queue.empty() || m_closed; });
        if (m_queue.empty()) return std::nullopt;
        T item = std::move(m_queue.front());
        m_queue.pop();
        m_not_full.notify_one();
        return item;
    }

    /** Signal no more items will be pushed. Wakes all blocked threads. */
    void close()
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_closed = true;
        m_not_empty.notify_all();
        m_not_full.notify_all();
    }

    bool   is_closed() const { std::lock_guard<std::mutex> lk(m_mutex); return m_closed; }
    size_t size()      const { std::lock_guard<std::mutex> lk(m_mutex); return m_queue.size(); }

private:
    mutable std::mutex      m_mutex;
    std::condition_variable m_not_full;
    std::condition_variable m_not_empty;
    std::queue<T>           m_queue;
    size_t                  m_capacity;
    bool                    m_closed{false};
};
