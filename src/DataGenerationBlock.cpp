#include "DataGenerationBlock.h"
#include <thread>

DataGenerationBlock::DataGenerationBlock(
    std::unique_ptr<IDataSource>             source,
    std::shared_ptr<BoundedQueue<PixelPair>> output_queue,
    int64_t                                  interval_ns,
    uint64_t                                 max_pairs)
    : m_source(std::move(source))
    , m_output(std::move(output_queue))
    , m_interval_ns(interval_ns)
    , m_max_pairs(max_pairs)
{}

DataGenerationBlock::~DataGenerationBlock() { stop(); }

void DataGenerationBlock::start()
{
    m_done    = false;
    m_running = true;
    m_thread  = std::thread(&DataGenerationBlock::run, this);
}

void DataGenerationBlock::stop()
{
    m_running = false;
    m_output->close();
    if (m_thread.joinable())
        m_thread.join();
}

void DataGenerationBlock::wait_until_done()
{
    std::unique_lock<std::mutex> lk(m_done_mutex);
    m_done_cv.wait(lk, [this]{ return m_done; });
}

void DataGenerationBlock::run()
{
    using clock = std::chrono::steady_clock;
    using ns    = std::chrono::nanoseconds;
    const ns interval(m_interval_ns);
    auto next_tick = clock::now();

    while (m_running)
    {
        std::this_thread::sleep_until(next_tick);
        if (!m_running) break;
        auto tick_start = clock::now();
        uint8_t p1{}, p2{};
        if (!m_source->next(p1, p2)) { m_output->close(); break; }
        PixelPair pair(p1, p2, tick_start);
        if (!m_output->push(pair)) break;
        uint64_t emitted = ++m_pairs_emitted;
        auto elapsed = std::chrono::duration_cast<ns>(clock::now()-tick_start).count();
        int64_t prev = m_worst_cycle_ns.load(std::memory_order_relaxed);
        while (elapsed > prev && !m_worst_cycle_ns.compare_exchange_weak(prev, elapsed, std::memory_order_relaxed)) {}
        if (m_max_pairs > 0 && emitted >= m_max_pairs) { m_output->close(); break; }
        next_tick += interval;
    }
    { std::lock_guard<std::mutex> lk(m_done_mutex); m_done = true; }
    m_done_cv.notify_all();
}
