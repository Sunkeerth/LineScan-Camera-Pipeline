#pragma once
#include "IDataSource.h"
#include "BoundedQueue.h"
#include "PixelPair.h"
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstdint>
#include <chrono>

class DataGenerationBlock
{
public:
    DataGenerationBlock(std::unique_ptr<IDataSource>             source,
                        std::shared_ptr<BoundedQueue<PixelPair>> output_queue,
                        int64_t                                  interval_ns,
                        uint64_t                                 max_pairs = 0);
    ~DataGenerationBlock();
    void start();
    void stop();
    void wait_until_done();
    uint64_t pairs_emitted()  const { return m_pairs_emitted.load(); }
    int64_t  worst_cycle_ns() const { return m_worst_cycle_ns.load(); }
private:
    void run();
    std::unique_ptr<IDataSource>             m_source;
    std::shared_ptr<BoundedQueue<PixelPair>> m_output;
    int64_t                                  m_interval_ns;
    uint64_t                                 m_max_pairs;
    std::thread           m_thread;
    std::atomic<bool>     m_running{false};
    std::atomic<uint64_t> m_pairs_emitted{0};
    std::atomic<int64_t>  m_worst_cycle_ns{0};
    std::mutex              m_done_mutex;
    std::condition_variable m_done_cv;
    bool                    m_done{false};
};
