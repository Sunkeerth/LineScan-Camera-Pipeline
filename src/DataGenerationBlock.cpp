// ============================================================================
// DataGenerationBlock.cpp
// Implementation of Block 1 – Data Generation.
// Produces PixelPair objects at precise intervals (T nanoseconds) using
// sleep_until to avoid clock drift. Reads from an IDataSource (RNG, CSV, Webcam).
// ============================================================================

#include "DataGenerationBlock.h"
#include <thread>   // std::this_thread::sleep_until

// ----------------------------------------------------------------------------
// Constructor: stores the source, output queue, timing interval, and optional
// maximum number of pairs.
// ----------------------------------------------------------------------------
DataGenerationBlock::DataGenerationBlock(
    std::unique_ptr<IDataSource>             source,      // pixel source (ownership transferred)
    std::shared_ptr<BoundedQueue<PixelPair>> output_queue, // queue to push into
    int64_t                                  interval_ns,  // T nanoseconds between pairs
    uint64_t                                 max_pairs)    // 0 = unlimited
    : m_source(std::move(source))      // take ownership of the source
    , m_output(std::move(output_queue)) // store shared pointer to queue
    , m_interval_ns(interval_ns)
    , m_max_pairs(max_pairs)
{}

// ----------------------------------------------------------------------------
// Destructor: ensures the thread is stopped.
// ----------------------------------------------------------------------------
DataGenerationBlock::~DataGenerationBlock() { stop(); }

// ----------------------------------------------------------------------------
// Starts the producer thread.
// ----------------------------------------------------------------------------
void DataGenerationBlock::start()
{
    m_done    = false;                 // not done yet
    m_running = true;                  // thread should run
    m_thread  = std::thread(&DataGenerationBlock::run, this); // launch run() in new thread
}

// ----------------------------------------------------------------------------
// Signals the thread to stop, closes the output queue, and waits for thread exit.
// ----------------------------------------------------------------------------
void DataGenerationBlock::stop()
{
    m_running = false;                 // tell run() loop to exit
    m_output->close();                 // unblock any waiting consumers (Block 2)
    if (m_thread.joinable())
        m_thread.join();               // wait for thread to finish
}

// ----------------------------------------------------------------------------
// Blocks until the thread has finished (used by Pipeline to wait for completion).
// ----------------------------------------------------------------------------
void DataGenerationBlock::wait_until_done()
{
    std::unique_lock<std::mutex> lk(m_done_mutex);
    m_done_cv.wait(lk, [this]{ return m_done; }); // wait until m_done == true
}

// ----------------------------------------------------------------------------
// Main worker loop. Runs in its own thread.
// ----------------------------------------------------------------------------
void DataGenerationBlock::run()
{
    using clock = std::chrono::steady_clock;   // monotonic clock, unaffected by system time changes
    using ns    = std::chrono::nanoseconds;
    const ns interval(m_interval_ns);          // interval as a chrono duration
    auto next_tick = clock::now();             // first emission time = now

    while (m_running)
    {
        // Sleep until the exact time when the next pair should be emitted.
        // This is superior to sleep(T) because it compensates for the time spent
        // doing work inside the loop – no cumulative drift.
        std::this_thread::sleep_until(next_tick);
        if (!m_running) break;                 // stop signal received

        auto tick_start = clock::now();        // actual emission timestamp (used for latency checks)

        uint8_t p1{}, p2{};
        if (!m_source->next(p1, p2))           // try to get next two pixels
        {
            m_output->close();                 // no more data → close queue
            break;
        }

        PixelPair pair(p1, p2, tick_start);    // envelope with pixels and timestamp
        if (!m_output->push(pair)) break;      // push to queue; if queue closed, exit

        uint64_t emitted = ++m_pairs_emitted;  // atomic increment

        // Measure how long the push operation took (from tick start to now)
        auto elapsed = std::chrono::duration_cast<ns>(clock::now() - tick_start).count();

        // Update worst cycle overrun (atomic compare‑exchange)
        int64_t prev = m_worst_cycle_ns.load(std::memory_order_relaxed);
        while (elapsed > prev &&
               !m_worst_cycle_ns.compare_exchange_weak(prev, elapsed, std::memory_order_relaxed))
        {}

        // If max_pairs limit is reached, close the queue and stop
        if (m_max_pairs > 0 && emitted >= m_max_pairs)
        {
            m_output->close();
            break;
        }

        // Schedule next tick exactly interval nanoseconds after the previous tick
        next_tick += interval;
    }

    // Mark as done and notify anyone waiting in wait_until_done()
    { std::lock_guard<std::mutex> lk(m_done_mutex); m_done = true; }
    m_done_cv.notify_all();
}