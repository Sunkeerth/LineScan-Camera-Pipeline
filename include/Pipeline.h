#pragma once
#include "DataGenerationBlock.h"
#include "FilterThresholdBlock.h"
#include "BoundedQueue.h"
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct PipelineConfig
{
    // ── Core ─────────────────────────────────────────────────────────────────
    int     columns{8};
    double  threshold_tv{100.0};
    int64_t interval_ns{500};

    // ── Source ────────────────────────────────────────────────────────────────
    bool        test_mode{false};
    std::string csv_path{};
    bool        webcam_mode{false};
    int         webcam_device{2};      // /dev/video2 = external USB webcam
    int         webcam_rows{0};

    // ── Run control ───────────────────────────────────────────────────────────
    uint64_t    max_pairs{0};          // 0 = run until Ctrl+C
    bool        verbose{false};

    // ── Output (all saved inside project data/ folder) ────────────────────────
    std::string project_data_dir{"../data"};  // relative to build/
};

class Pipeline
{
public:
    explicit Pipeline(PipelineConfig cfg);
    void run();
    void request_stop();
    void print_profile() const;
    const std::vector<FilteredOutput>& outputs() const { return m_outputs; }

private:
    void save_results() const;

    PipelineConfig m_cfg;
    std::shared_ptr<BoundedQueue<PixelPair>> m_queue;
    std::unique_ptr<DataGenerationBlock>     m_data_gen;
    std::unique_ptr<FilterThresholdBlock>    m_filter;
    std::unique_ptr<std::mutex>              m_output_mutex;
    std::vector<FilteredOutput>              m_outputs;
    std::string                              m_run_id;   // timestamp for this run
};