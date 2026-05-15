// ============================================================================
// BoundedQueue.h
// Thread-safe bounded FIFO queue (Producer-Consumer pattern).
// Used as the bridge between Block 1 and Block 2 (and later between Block 3/4).
// Capacity is typically set to m (number of columns) to keep memory bounded.
// ============================================================================

#pragma once   // Ensures this header is included only once per translation unit.

#include <queue>          // std::queue
#include <mutex>          // std::mutex, std::unique_lock, std::lock_guard
#include <condition_variable> // std::condition_variable
#include <optional>       // std::optional (C++17) – used for pop() return
#include <chrono>         // (not directly used here, but often needed for timing)

/**
 * @brief Thread-safe bounded FIFO queue.
 *
 * @tparam T Type of elements stored in the queue.
 *
 * Producer threads call push(), consumer threads call pop().
 * If the queue is full, push() blocks until space becomes available.
 * If the queue is empty, pop() blocks until an item arrives or the queue is closed.
 */
template<typename T>
class BoundedQueue
{
public:
    /**
     * @brief Construct a new Bounded Queue.
     * @param capacity Maximum number of elements the queue can hold.
     */
    explicit BoundedQueue(size_t capacity = 64) : m_capacity(capacity) {}

    // Disable copy constructor and copy assignment (cannot copy a mutex).
    BoundedQueue(const BoundedQueue&)            = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /**
     * @brief Push an item into the queue.
     * @param item The item to be pushed (moved).
     * @return false if the queue was closed before the push could complete,
     *         true otherwise.
     *
     * Blocks if the queue is full, until space is available or the queue closes.
     */
    bool push(T item)
    {
        std::unique_lock<std::mutex> lk(m_mutex);      // Lock the mutex.
        // Wait while the queue is full AND the queue is not closed.
        m_not_full.wait(lk, [this]{ return m_queue.size() < m_capacity || m_closed; });
        if (m_closed) return false;                    // Queue closed → reject push.
        m_queue.push(std::move(item));                 // Store the item.
        m_not_empty.notify_one();                      // Wake one waiting consumer.
        return true;
    }

    /**
     * @brief Pop an item from the queue.
     * @return std::optional<T> containing the item if successful,
     *         std::nullopt if the queue is closed and empty.
     *
     * Blocks if the queue is empty, until an item arrives or the queue closes.
     */
    std::optional<T> pop()
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        // Wait while the queue is empty AND the queue is not closed.
        m_not_empty.wait(lk, [this]{ return !m_queue.empty() || m_closed; });
        if (m_queue.empty()) return std::nullopt;      // Queue closed and empty.
        T item = std::move(m_queue.front());           // Take the front item.
        m_queue.pop();                                 // Remove it from the queue.
        m_not_full.notify_one();                       // Wake one waiting producer.
        return item;
    }

    /**
     * @brief Close the queue. No more items can be pushed.
     *        Wakes all blocked threads so they can exit gracefully.
     */
    void close()
    {
        std::lock_guard<std::mutex> lk(m_mutex);       // Lock the mutex.
        m_closed = true;                               // Set closed flag.
        m_not_empty.notify_all();                      // Wake all consumers.
        m_not_full.notify_all();                       // Wake all producers.
    }

    /** @brief Check if the queue is closed. */
    bool   is_closed() const { std::lock_guard<std::mutex> lk(m_mutex); return m_closed; }

    /** @brief Return the current number of elements in the queue. */
    size_t size()      const { std::lock_guard<std::mutex> lk(m_mutex); return m_queue.size(); }

private:
    mutable std::mutex      m_mutex;          // Protects all shared data.
    std::condition_variable m_not_full;       // Signalled when space becomes available.
    std::condition_variable m_not_empty;      // Signalled when an item becomes available.
    std::queue<T>           m_queue;          // Underlying FIFO container.
    size_t                  m_capacity;       // Maximum allowed size.
    bool                    m_closed{false};  // True after close() is called.
};