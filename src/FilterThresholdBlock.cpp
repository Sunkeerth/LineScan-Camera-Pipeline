// ============================================================================
// FilterThresholdBlock.cpp
// Implementation of Block 2 – Filter & Threshold.
// For each incoming pixel pair, applies a 9‑tap Gaussian filter (smoothing)
// and compares the filtered value to a threshold TV. Outputs 0 (OK) or 1 (defect).
// Maintains a sliding window (deque) to hold the last 9 pixel values.
// ============================================================================

#include "FilterThresholdBlock.h"
#include <numeric>   // not strictly used here, but kept from original

// The static filter kernel is defined in the header; this line ensures its storage.
constexpr std::array<double, 9> FilterThresholdBlock::FILTER_WINDOW;

// ----------------------------------------------------------------------------
// Constructor: stores input queue, threshold, and optional output callback.
// ----------------------------------------------------------------------------
FilterThresholdBlock::FilterThresholdBlock(
    std::shared_ptr<BoundedQueue<PixelPair>> input_queue,   // queue from Block 1
    double threshold_tv,                                    // defect threshold (TV)
    OutputCallback on_output)                               // callback for each filtered pixel
    : m_input(std::move(input_queue))
    , m_threshold_tv(threshold_tv)
    , m_on_output(std::move(on_output))
{}

FilterThresholdBlock::~FilterThresholdBlock() { stop(); }

// ----------------------------------------------------------------------------
// Starts the processing thread.
// ----------------------------------------------------------------------------
void FilterThresholdBlock::start()
{
    m_running = true;
    m_thread  = std::thread(&FilterThresholdBlock::run, this);
}

// ----------------------------------------------------------------------------
// Stops the thread.
// ----------------------------------------------------------------------------
void FilterThresholdBlock::stop()
{
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

// ----------------------------------------------------------------------------
// Main processing loop: pops PixelPair objects, feeds each pixel into a sliding
// window, applies convolution and thresholding, and calls the callback.
// Also handles edge pixels by padding (repeating border values) when the window
// is not yet full or after the stream ends.
// ----------------------------------------------------------------------------
void FilterThresholdBlock::run()
{
    using clock = std::chrono::steady_clock;
    using ns    = std::chrono::nanoseconds;

    while (true)
    {
        auto maybe = m_input->pop();          // block until a PixelPair arrives or queue closed
        if (!maybe.has_value()) break;        // queue closed and empty → exit

        // Push both pixels of the pair into the sliding window (deque)
        m_ring.push_back(maybe->p1);
        m_ring.push_back(maybe->p2);

        // While we have at least 9 pixels (HALF_WINDOW*2+1 = 9), process the centre pixel
        while (m_ring.size() >= static_cast<size_t>(2 * HALF_WINDOW + 1))
        {
            double filtered = 0.0;
            // 9‑tap convolution: multiply each pixel in the window by the corresponding kernel weight
            for (int i = 0; i < 9; ++i)
                filtered += static_cast<double>(m_ring[i]) * FILTER_WINDOW[i];

            // Threshold: 1 if filtered >= TV, else 0
            uint8_t thresholded = (filtered >= m_threshold_tv) ? 1u : 0u;

            FilteredOutput out;
            out.original      = m_ring[HALF_WINDOW];  // the centre pixel (index 4)
            out.filtered      = filtered;
            out.thresholded   = thresholded;
            out.output_time   = clock::now();

            if (m_on_output) m_on_output(out);        // send to callback (which pushes to Block 3)

            // Measure inter‑output gap (latency between consecutive output pixels)
            if (!m_first_output) {
                auto gap = std::chrono::duration_cast<ns>(out.output_time - m_prev_output_time).count();
                int64_t prev = m_worst_latency_ns.load(std::memory_order_relaxed);
                while (gap > prev &&
                       !m_worst_latency_ns.compare_exchange_weak(prev, gap, std::memory_order_relaxed))
                {}
            }
            m_prev_output_time = out.output_time;
            m_first_output = false;

            ++m_pixels_output;
            m_ring.pop_front();    // remove the oldest pixel (slide window forward)
        }
    }

    // -------------------------------
    // Flush remaining pixels (tail of the stream)
    // For the last few pixels (<9), we pad by repeating the last available pixel.
    // This ensures the convolution can still run.
    // -------------------------------
    while (!m_ring.empty())
    {
        // If window is not full, pad by duplicating the last pixel value
        while (m_ring.size() < static_cast<size_t>(2*HALF_WINDOW+1))
            m_ring.push_back(m_ring.back());

        double filtered = 0.0;
        for (int i = 0; i < 9; ++i)
            filtered += static_cast<double>(m_ring[i]) * FILTER_WINDOW[i];

        uint8_t thresholded = (filtered >= m_threshold_tv) ? 1u : 0u;
        FilteredOutput out;
        out.original    = m_ring[HALF_WINDOW];
        out.filtered    = filtered;
        out.thresholded = thresholded;
        out.output_time = clock::now();
        if (m_on_output) m_on_output(out);
        ++m_pixels_output;
        m_ring.pop_front();
    }
}