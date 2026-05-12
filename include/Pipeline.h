#pragma once
#include "DataGenerationBlock.h"
#include "FilterThresholdBlock.h"
#include "LabellingBlock.h"
#include "TracingBlock.h"
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
    int         webcam_device{2};
    int         webcam_rows{0};

    // ── Run control ───────────────────────────────────────────────────────────
    uint64_t    max_pairs{0};
    bool        verbose{false};

    // ── Output ────────────────────────────────────────────────────────────────
    std::string project_data_dir{"../data"};
};

class Pipeline
{
public:
    explicit Pipeline(PipelineConfig cfg);
    void run();
    void request_stop();
    void print_profile() const;
    const std::vector<FilteredOutput>&   outputs()    const { return m_outputs; }
    const std::vector<ComponentResult>&  components() const { return m_components; }

private:
    void save_results() const;

    PipelineConfig m_cfg;

    // Block 1 → Block 2 queue
    std::shared_ptr<BoundedQueue<PixelPair>>      m_queue;

    // Block 2 → Block 3 queue
    std::shared_ptr<BoundedQueue<FilteredOutput>>  m_label_queue;

    // Block 3 → Block 4 queue
    std::shared_ptr<BoundedQueue<LabelledPixel>>  m_trace_queue;

    // Blocks
    std::unique_ptr<DataGenerationBlock>   m_data_gen;
    std::unique_ptr<FilterThresholdBlock>  m_filter;
    std::unique_ptr<LabellingBlock>        m_labeller;
    std::unique_ptr<TracingBlock>          m_tracer;

    std::unique_ptr<std::mutex>            m_output_mutex;
    std::vector<FilteredOutput>            m_outputs;
    std::vector<ComponentResult>           m_components;
    std::string                            m_run_id;
};
