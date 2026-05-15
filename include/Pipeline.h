// ============================================================================
// Pipeline.h
// Orchestrates all four blocks: DataGeneration → FilterThreshold → Labelling → Tracing.
// Creates and connects the three bounded queues, starts the threads,
// waits for completion, and saves final results (components.csv).
// ============================================================================

#pragma once

#include "DataGenerationBlock.h"   // Block 1
#include "FilterThresholdBlock.h"  // Block 2
#include "LabellingBlock.h"        // Block 3
#include "TracingBlock.h"          // Block 4
#include "BoundedQueue.h"          // Thread‑safe queues between blocks
#include <memory>                  // std::shared_ptr, std::unique_ptr
#include <mutex>                   // std::mutex
#include <string>                  // std::string
#include <vector>                  // std::vector for storing outputs

/**
 * @brief Configuration structure for the pipeline.
 * All parameters are parsed from command line (see main.cpp).
 */
struct PipelineConfig
{
    // ── Core ─────────────────────────────────────────────────────────────────
    int     columns{8};           // Number of columns per row (m). Must be even.
    double  threshold_tv{100.0};  // Threshold value for defect detection (TV).
    int64_t interval_ns{500};     // Time between pixel pair emissions (T nanoseconds).

    // ── Source ────────────────────────────────────────────────────────────────
    bool        test_mode{false};    // If true, read from CSV file.
    std::string csv_path{};          // Path to CSV file (used in test mode).
    bool        webcam_mode{false};  // If true, read from live webcam.
    int         webcam_device{2};    // Webcam device index (e.g., /dev/video2).
    int         webcam_rows{0};      // Desired number of rows (0 = auto from aspect ratio).

    // ── Run control ───────────────────────────────────────────────────────────
    uint64_t    max_pairs{0};        // Maximum pixel pairs to process (0 = unlimited).
    bool        verbose{false};      // Print progress and debug messages.

    // ── Output ────────────────────────────────────────────────────────────────
    std::string project_data_dir{"../data"};  // Directory to save results.
};

/**
 * @brief Main pipeline controller.
 * Connects blocks, starts threads, waits for shutdown, saves components.
 */
class Pipeline
{
public:
    explicit Pipeline(PipelineConfig cfg);
    void run();                      // Start all blocks and wait until done.
    void request_stop();             // Signal all blocks to stop (e.g., on Ctrl+C).
    void print_profile() const;      // Print performance statistics (latency, cycles).

    // Getters for results (used after run() completes).
    const std::vector<FilteredOutput>&   outputs()    const { return m_outputs; }
    const std::vector<ComponentResult>&  components() const { return m_components; }

private:
    void save_results() const;   // Write components.csv and other output files.

    PipelineConfig m_cfg;   // User configuration.

    // ─── Three queues between the four blocks ──────────────────────────────
    // Block 1 → Block 2: passes PixelPair (two raw pixels + timestamp)
    std::shared_ptr<BoundedQueue<PixelPair>>      m_queue;

    // Block 2 → Block 3: passes FilteredOutput (original, filtered, thresholded value)
    std::shared_ptr<BoundedQueue<FilteredOutput>>  m_label_queue;

    // Block 3 → Block 4: passes LabelledPixel (label, merge events)
    std::shared_ptr<BoundedQueue<LabelledPixel>>  m_trace_queue;

    // ─── The four blocks (owned by Pipeline) ───────────────────────────────
    std::unique_ptr<DataGenerationBlock>   m_data_gen;
    std::unique_ptr<FilterThresholdBlock>  m_filter;
    std::unique_ptr<LabellingBlock>        m_labeller;
    std::unique_ptr<TracingBlock>          m_tracer;

    // ─── Output storage ────────────────────────────────────────────────────
    std::unique_ptr<std::mutex>            m_output_mutex;  // Mutex for thread‑safe accumulation.
    std::vector<FilteredOutput>            m_outputs;       // All filtered outputs (if needed).
    std::vector<ComponentResult>           m_components;    // Final components (blobs).
    std::string                            m_run_id;        // Timestamped run directory.
};