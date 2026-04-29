#include "FilterThresholdBlock.h"
#include <numeric>

constexpr std::array<double, 9> FilterThresholdBlock::FILTER_WINDOW;

FilterThresholdBlock::FilterThresholdBlock(
    std::shared_ptr<BoundedQueue<PixelPair>> input_queue,
    double threshold_tv, OutputCallback on_output)
    : m_input(std::move(input_queue))
    , m_threshold_tv(threshold_tv)
    , m_on_output(std::move(on_output))
{}

FilterThresholdBlock::~FilterThresholdBlock() { stop(); }

void FilterThresholdBlock::start()
{
    m_running = true;
    m_thread  = std::thread(&FilterThresholdBlock::run, this);
}

void FilterThresholdBlock::stop()
{
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void FilterThresholdBlock::run()
{
    using clock = std::chrono::steady_clock;
    using ns    = std::chrono::nanoseconds;

    while (true)
    {
        auto maybe = m_input->pop();
        if (!maybe.has_value()) break;
        m_ring.push_back(maybe->p1);
        m_ring.push_back(maybe->p2);
        while (m_ring.size() >= static_cast<size_t>(2 * HALF_WINDOW + 1))
        {
            double filtered = 0.0;
            for (int i = 0; i < 9; ++i)
                filtered += static_cast<double>(m_ring[i]) * FILTER_WINDOW[i];
            uint8_t thresholded = (filtered >= m_threshold_tv) ? 1u : 0u;
            FilteredOutput out;
            out.original=m_ring[HALF_WINDOW]; out.filtered=filtered;
            out.thresholded=thresholded; out.output_time=clock::now();
            if (m_on_output) m_on_output(out);
            if (!m_first_output) {
                auto gap = std::chrono::duration_cast<ns>(out.output_time-m_prev_output_time).count();
                int64_t prev = m_worst_latency_ns.load(std::memory_order_relaxed);
                while (gap > prev && !m_worst_latency_ns.compare_exchange_weak(prev, gap, std::memory_order_relaxed)) {}
            }
            m_prev_output_time=out.output_time; m_first_output=false;
            ++m_pixels_output; m_ring.pop_front();
        }
    }
    while (!m_ring.empty())
    {
        while (m_ring.size() < static_cast<size_t>(2*HALF_WINDOW+1)) m_ring.push_back(m_ring.back());
        double filtered = 0.0;
        for (int i = 0; i < 9; ++i) filtered += static_cast<double>(m_ring[i])*FILTER_WINDOW[i];
        uint8_t thresholded = (filtered >= m_threshold_tv) ? 1u : 0u;
        FilteredOutput out;
        out.original=m_ring[HALF_WINDOW]; out.filtered=filtered;
        out.thresholded=thresholded; out.output_time=clock::now();
        if (m_on_output) m_on_output(out);
        ++m_pixels_output; m_ring.pop_front();
    }
}
