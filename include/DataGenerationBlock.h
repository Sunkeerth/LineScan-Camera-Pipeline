// ============================================================================
// DataGenerationBlock.h
// Block 1 – Data Generation.
// Simulates a line‑scan camera: reads pixel pairs from an IDataSource
// and pushes them into a BoundedQueue at a fixed interval (T nanoseconds).
// ============================================================================

#pragma once

#include "IDataSource.h"               // Strategy for reading pixel pairs.
#include "BoundedQueue.h"              // Thread‑safe output queue.
#include "PixelPair.h"                 // Structure holding two pixels + timestamp.

#include <memory>      // std::unique_ptr, std::shared_ptr
#include <thread>      // std::thread
#include <atomic>      // std::atomic
#include <mutex>       // std::mutex, std::condition_variable
#include <condition_variable>
#include <cstdint>     // uint8_t, uint64_t, int64_t
#include <chrono>      // std::chrono::nanoseconds, etc.

/**
 * @brief Produces PixelPair objects at a precise rate.
 *
 * Runs in its own thread. Uses sleep_until to maintain an exact interval,
 * avoiding drift. The data source can be CSV, RNG, or webcam (Strategy Pattern).
 */
class DataGenerationBlock
{
public:
    /**
     * @brief Construct the Data Generation Block.
     * @param source       Data source (RNG, CSV, etc.) – ownership transferred.
     * @param output_queue Queue where produced PixelPairs will be pushed.
     * @param interval_ns  Time between successive emissions (T nanoseconds).
     * @param max_pairs    Maximum number of pairs to produce (0 = unlimited).
     */
    DataGenerationBlock(std::unique_ptr<IDataSource>             source,
                        std::shared_ptr<BoundedQueue<PixelPair>> output_queue,
                        int64_t                                  interval_ns,
                        uint64_t                                 max_pairs = 0);

    ~DataGenerationBlock();     // Stops the thread and waits for it to finish.

    void start();               // Launches the producer thread.
    void stop();                // Signals the thread to stop and closes the queue.
    void wait_until_done();     // Blocks until the thread has fully stopped.

    /** @brief Number of pixel pairs emitted so far. */
    uint64_t pairs_emitted()  const { return m_pairs_emitted.load(); }

    /** @brief Worst observed cycle overrun (in nanoseconds). */
    int64_t  worst_cycle_ns() const { return m_worst_cycle_ns.load(); }

private:
    void run();   // Main loop executed by the thread.

    std::unique_ptr<IDataSource>             m_source;      // Source of pixel data.
    std::shared_ptr<BoundedQueue<PixelPair>> m_output;      // Queue to push into.
    int64_t                                  m_interval_ns; // Emission period.
    uint64_t                                 m_max_pairs;   // Limit (0 = no limit).

    std::thread           m_thread;          // The worker thread.
    std::atomic<bool>     m_running{false};  // Set to false to stop the loop.
    std::atomic<uint64_t> m_pairs_emitted{0};// Count of emitted pairs.
    std::atomic<int64_t>  m_worst_cycle_ns{0};// Maximum cycle overrun.

    std::mutex              m_done_mutex;    // Used with m_done_cv.
    std::condition_variable m_done_cv;       // Signalled when thread exits.
    bool                    m_done{false};   // True after thread has stopped.
};