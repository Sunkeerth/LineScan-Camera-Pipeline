// ============================================================================
// FilterThresholdBlock.h
// Block 2 – Filter & Threshold Block.
// For each incoming pixel: applies a 9‑tap Gaussian filter (smoothing)
// and compares the filtered value to a threshold TV.
// Outputs 1 (defect) if filtered >= TV, else 0.
// ============================================================================

#pragma once

#include "BoundedQueue.h"   // Input queue (receives PixelPair from Block 1).
#include "PixelPair.h"      // Structure holding two pixels (though we process one at a time).

#include <array>            // std::array for fixed‑size filter kernel.
#include <deque>            // std::deque for sliding window of pixel values.
#include <memory>           // std::shared_ptr.
#include <thread>           // std::thread.
#include <atomic>           // std::atomic.
#include <cstdint>          // uint8_t, etc.
#include <functional>       // std::function for callback.
#include <chrono>           // std::chrono::steady_clock::time_point.

/**
 * @brief Output record for a single processed pixel.
 */
struct FilteredOutput
{
    uint8_t  original{0};                               // Raw pixel value (0‑255).
    double   filtered{0.0};                             // Smoothed value after Gaussian.
    uint8_t  thresholded{0};                            // 1 = defect, 0 = ok.
    std::chrono::steady_clock::time_point output_time{};// Timestamp for latency measurement.
};

/**
 * @brief Applies convolution and thresholding to a stream of pixels.
 *
 * Maintains a sliding window of 9 pixels (current pixel ±4 neighbours).
 * Edge pixels are padded by repeating the border value.
 * The result for each pixel is passed to an optional callback (Observer pattern).
 */
class FilterThresholdBlock
{
public:
    // 9‑tap Gaussian filter kernel (sum ≈ 1.0). Values are for a sigma of about 1.0.
    static constexpr std::array<double, 9> FILTER_WINDOW = {
        0.00025177, 0.008666992, 0.078025818,
        0.24130249, 0.343757629, 0.24130249,
        0.078025818, 0.008666992, 0.000125885
    };
    static constexpr int HALF_WINDOW = 4;   // Half of the window size (9 -> 4 on each side).

    /**
     * @brief Type of callback invoked for each processed pixel.
     * @param filtered_output Contains original, filtered, thresholded values and timestamp.
     */
    using OutputCallback = std::function<void(const FilteredOutput&)>;

    /**
     * @brief Construct the Filter & Threshold Block.
     * @param input_queue   Queue from which PixelPair objects are popped.
     * @param threshold_tv  Threshold value (TV) for defect decision.
     * @param on_output     Optional callback to receive every output.
     */
    FilterThresholdBlock(std::shared_ptr<BoundedQueue<PixelPair>> input_queue,
                         double                                    threshold_tv,
                         OutputCallback                           on_output = nullptr);
    ~FilterThresholdBlock();

    void start();   // Launches the processing thread.
    void stop();    // Signals the thread to stop and closes the queue.

    /** @brief Number of pixels processed and output. */
    uint64_t pixels_output()    const { return m_pixels_output.load(); }

    /** @brief Worst observed latency (time from pixel emission to output) in ns. */
    int64_t  worst_latency_ns() const { return m_worst_latency_ns.load(); }

private:
    void run();   // Main loop executed by the thread.

    std::shared_ptr<BoundedQueue<PixelPair>> m_input;      // Queue of incoming pixel pairs.
    double         m_threshold_tv;                         // Cut‑off value for defect detection.
    OutputCallback m_on_output;                            // User callback (optional).

    std::deque<uint8_t> m_ring;        // Sliding window of up to 9 pixel values.
                                       // We store only the raw values needed for convolution.

    std::thread           m_thread;            // Worker thread.
    std::atomic<bool>     m_running{false};    // Set to false to stop the loop.
    std::atomic<uint64_t> m_pixels_output{0};  // Count of processed pixels.
    std::atomic<int64_t>  m_worst_latency_ns{0};// Maximum observed latency.

    std::chrono::steady_clock::time_point m_prev_output_time{}; // For latency calculation.
    bool                                  m_first_output{true}; // True before first output.
};