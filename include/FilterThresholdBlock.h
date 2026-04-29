
// FilterThresholdBlock.h
#pragma once
#include "BoundedQueue.h"
#include "PixelPair.h"

#include <array>
#include <deque>
#include <memory>
#include <thread>
#include <atomic>
#include <cstdint>
#include <functional>
#include <chrono>

/** Output record for each processed pixel. */
struct FilteredOutput
{
    uint8_t  original{0};
    double   filtered{0.0};
    uint8_t  thresholded{0};   ///< 1 = defect, 0 = ok
    std::chrono::steady_clock::time_point output_time{};
};

/**
 * @brief Block 2 — Filter & Threshold Block.
 *
 * For each pixel K: dot-product of 9-element window [K-4..K+4] with
 * Gaussian filter → compare to TV → output 0 or 1.
 * Edge pixels are padded by repeating the border value.
 */
class FilterThresholdBlock
{
public:
    static constexpr std::array<double, 9> FILTER_WINDOW = {
        0.00025177, 0.008666992, 0.078025818,
        0.24130249, 0.343757629, 0.24130249,
        0.078025818, 0.008666992, 0.000125885
    };
    static constexpr int HALF_WINDOW = 4;

    using OutputCallback = std::function<void(const FilteredOutput&)>;

    FilterThresholdBlock(std::shared_ptr<BoundedQueue<PixelPair>> input_queue,
                         double                                    threshold_tv,
                         OutputCallback                           on_output = nullptr);
    ~FilterThresholdBlock();

    void start();
    void stop();

    uint64_t pixels_output()    const { return m_pixels_output.load(); }
    int64_t  worst_latency_ns() const { return m_worst_latency_ns.load(); }

private:
    void run();

    std::shared_ptr<BoundedQueue<PixelPair>> m_input;
    double         m_threshold_tv;
    OutputCallback m_on_output;
    std::deque<uint8_t> m_ring;

    std::thread           m_thread;
    std::atomic<bool>     m_running{false};
    std::atomic<uint64_t> m_pixels_output{0};
    std::atomic<int64_t>  m_worst_latency_ns{0};

    std::chrono::steady_clock::time_point m_prev_output_time{};
    bool                                  m_first_output{true};
};
