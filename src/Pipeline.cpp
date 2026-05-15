// ============================================================================
// Pipeline.cpp
// Orchestrates the four blocks: creates input queues, instantiates each block,
// starts them in reverse order (consumers first), waits for completion, and
// saves results (components.csv, inference_report.txt, etc.).
// ============================================================================

#include "Pipeline.h"
#include "CsvDataSource.h"
#include "RngDataSource.h"
#include "WebcamDataSource.h"

#include <iostream>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <thread>
#include <stdexcept>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sys/stat.h>   // mkdir

// ----------------------------------------------------------------------------
// Helper: generate a timestamp string (YYYYMMDD_HHMMSS) for run directory.
// ----------------------------------------------------------------------------
static std::string make_timestamp()
{
    auto now  = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return std::string(buf);
}

// ----------------------------------------------------------------------------
// Helper: create a directory (cross‑platform using POSIX mkdir).
// ----------------------------------------------------------------------------
static void mkdir_p(const std::string& path)
{
    mkdir(path.c_str(), 0755);
}

// ----------------------------------------------------------------------------
// Constructor: validates config, creates queues, instantiates data source,
// and builds the four blocks with their callbacks.
// ----------------------------------------------------------------------------
Pipeline::Pipeline(PipelineConfig cfg) : m_cfg(std::move(cfg))
{
    if (m_cfg.interval_ns < 100)
        throw std::invalid_argument("Interval T must be >= 100 ns.");
    if (m_cfg.columns <= 0 || m_cfg.columns % 2 != 0)
        throw std::invalid_argument("Columns m must be a positive even number.");

    m_run_id = make_timestamp();   // unique ID for this run

    // ── Block 1 → Block 2 queue ──────────────────────────────────────────────
    // Capacity = number of columns (m). This bounds memory as required.
    m_queue = std::make_shared<BoundedQueue<PixelPair>>(
        static_cast<size_t>(m_cfg.columns));

    // ── Block 2 → Block 3 queue ──────────────────────────────────────────────
    // Larger capacity (4*m) to absorb bursts between filter and labelling.
    m_label_queue = std::make_shared<BoundedQueue<FilteredOutput>>(
        static_cast<size_t>(m_cfg.columns * 4));

    // ── Block 3 → Block 4 queue ──────────────────────────────────────────────
    m_trace_queue = std::make_shared<BoundedQueue<LabelledPixel>>(
        static_cast<size_t>(m_cfg.columns * 4));

    // ── Create the data source (Strategy pattern) ────────────────────────────
    std::string run_dir      = m_cfg.project_data_dir + "/run_" + m_run_id;
    std::string preview_path = run_dir + "/webcam_frame.png";   // only used for webcam

    std::unique_ptr<IDataSource> source;
    if (m_cfg.webcam_mode)
    {
        mkdir_p(m_cfg.project_data_dir);
        mkdir_p(run_dir);   // create run directory before saving preview image
        source = std::make_unique<WebcamDataSource>(
            m_cfg.columns, m_cfg.webcam_rows,
            m_cfg.webcam_device, preview_path);
    }
    else if (m_cfg.test_mode)
        source = std::make_unique<CsvDataSource>(m_cfg.csv_path, m_cfg.columns);
    else
        source = std::make_unique<RngDataSource>();

    // ── Block 1: DataGenerationBlock ─────────────────────────────────────────
    m_data_gen = std::make_unique<DataGenerationBlock>(
        std::move(source), m_queue, m_cfg.interval_ns, m_cfg.max_pairs);

    // Mutex for thread‑safe accumulation of outputs and components
    m_output_mutex = std::make_unique<std::mutex>();
    std::mutex* mtx = m_output_mutex.get();

    // ── Block 2: FilterThresholdBlock with callback that stores outputs ───────
    auto on_output = [this, mtx](const FilteredOutput& out)
    {
        {
            std::lock_guard<std::mutex> lk(*mtx);
            if (m_cfg.verbose)
                std::cout << "pixel=" << std::setw(3) << static_cast<int>(out.original)
                          << "  filtered=" << std::fixed << std::setprecision(4)
                          << out.filtered
                          << "  result=" << (out.thresholded ? "DEFECT" : "ok") << "\n";
            m_outputs.push_back(out);
        }
        // Forward to Block 3 (outside lock to avoid deadlock)
        m_label_queue->push(out);
    };

    m_filter = std::make_unique<FilterThresholdBlock>(
        m_queue, m_cfg.threshold_tv, on_output);

    // ── Block 3: LabellingBlock callback (sends LabelledPixel to Block 4) ─────
    auto on_labelled = [this](const LabelledPixel& lp)
    {
        m_trace_queue->push(lp);
    };

    m_labeller = std::make_unique<LabellingBlock>(
        m_label_queue, m_cfg.columns, on_labelled);

    // ── Block 4: TracingBlock callbacks ──────────────────────────────────────
    auto on_component = [this, mtx](const ComponentResult& r)
    {
        std::lock_guard<std::mutex> lk(*mtx);
        m_components.push_back(r);
        std::cout << "[Component] label=" << static_cast<int>(r.label)
                  << "  pixels=" << r.pixel_count
                  << "  bbox=(" << r.row_start << "," << r.col_start
                  << ")->(" << r.row_end << "," << r.col_end << ")\n";
    };

    auto on_recycle = [this](uint8_t label)
    {
        m_labeller->recycle_label(label);   // return label to Block 3's free pool
    };

    m_tracer = std::make_unique<TracingBlock>(
        m_trace_queue, m_cfg.columns, m_cfg.columns / 2,
        on_component, on_recycle);
}

// ----------------------------------------------------------------------------
// Starts all four blocks in reverse order (consumers first) so that queues
// have waiters before producers push. Then waits for Block 1 to finish.
// After that, shuts down downstream blocks and saves results.
// ----------------------------------------------------------------------------
void Pipeline::run()
{
    // Start Block 4, then Block 3, then Block 2, then Block 1.
    m_tracer->start();
    m_labeller->start();
    m_filter->start();
    m_data_gen->start();

    // Block until Block 1 finishes (source exhausted or Ctrl+C)
    m_data_gen->wait_until_done();

    // Shut down in pipeline order (source first, then downstream)
    m_filter->stop();
    m_label_queue->close();    // unblocks LabellingBlock::pop()
    m_labeller->stop();
    m_trace_queue->close();    // unblocks TracingBlock::pop()
    m_tracer->stop();

    save_results();            // write all output files
}

// ----------------------------------------------------------------------------
// Signal handler calls this to stop Block 1 gracefully.
// ----------------------------------------------------------------------------
void Pipeline::request_stop() { m_data_gen->stop(); }

// ----------------------------------------------------------------------------
// Writes four output files:
//   1. results.csv         – every pixel (index, raw, filtered, defect flag)
//   2. defects_only.csv    – only defect pixels
//   3. components.csv      – final connected components (label, size, bbox)
//   4. inference_report.txt – human‑readable summary (statistics, timing, verdict)
// Also prints a summary to the console.
// ----------------------------------------------------------------------------
void Pipeline::save_results() const
{
    std::string run_dir = m_cfg.project_data_dir + "/run_" + m_run_id;
    mkdir_p(m_cfg.project_data_dir);
    mkdir_p(run_dir);

    // Compute basic statistics
    int defects = 0, ok_pixels = 0;
    int min_val = 255, max_val = 0;
    double sum = 0.0;
    for (const auto& o : m_outputs)
    {
        if (o.thresholded == 1) ++defects; else ++ok_pixels;
        int v = static_cast<int>(o.original);
        if (v < min_val) min_val = v;
        if (v > max_val) max_val = v;
        sum += v;
    }
    double avg        = m_outputs.empty() ? 0.0 : sum / m_outputs.size();
    double defect_pct = m_outputs.empty() ? 0.0 : (100.0 * defects / m_outputs.size());

    // 1. All pixels CSV
    {
        std::ofstream f(run_dir + "/results.csv");
        f << "pixel_index,raw_value,filtered_value,defect\n";
        for (size_t i = 0; i < m_outputs.size(); ++i)
            f << i << ","
              << static_cast<int>(m_outputs[i].original) << ","
              << std::fixed << std::setprecision(6) << m_outputs[i].filtered << ","
              << static_cast<int>(m_outputs[i].thresholded) << "\n";
    }

    // 2. Defects‑only CSV
    {
        std::ofstream f(run_dir + "/defects_only.csv");
        f << "pixel_index,raw_value,filtered_value\n";
        for (size_t i = 0; i < m_outputs.size(); ++i)
            if (m_outputs[i].thresholded == 1)
                f << i << ","
                  << static_cast<int>(m_outputs[i].original) << ","
                  << std::fixed << std::setprecision(6)
                  << m_outputs[i].filtered << "\n";
    }

    // 3. Components CSV (Phase 2 output – one row per blob)
    {
        std::ofstream f(run_dir + "/components.csv");
        f << "label,pixel_count,row_start,col_start,row_end,col_end\n";
        for (const auto& c : m_components)
            f << static_cast<int>(c.label) << ","
              << c.pixel_count << ","
              << c.row_start << "," << c.col_start << ","
              << c.row_end   << "," << c.col_end   << "\n";
    }

    // 4. Inference report (human readable)
    {
        std::ofstream f(run_dir + "/inference_report.txt");
        f << "==========================================================\n";
        f << "   CynLr Pipeline — Inference Report\n";
        f << "   Run ID : " << m_run_id << "\n";
        f << "==========================================================\n\n";

        f << "SOURCE         : ";
        if (m_cfg.webcam_mode)
            f << "Webcam (/dev/video" << m_cfg.webcam_device << ")\n";
        else if (m_cfg.test_mode)
            f << "CSV (" << m_cfg.csv_path << ")\n";
        else
            f << "RNG (random)\n";

        f << "COLUMNS (m)    : " << m_cfg.columns      << "\n";
        f << "THRESHOLD (TV) : " << m_cfg.threshold_tv << "\n";
        f << "INTERVAL T     : " << m_cfg.interval_ns  << " ns\n\n";

        f << "──────── PIXEL STATISTICS ────────\n";
        f << "Total pixels   : " << m_outputs.size()  << "\n";
        f << "Total pairs    : " << m_data_gen->pairs_emitted() << "\n";
        f << "Min value      : " << min_val << "\n";
        f << "Max value      : " << max_val << "\n";
        f << "Average value  : " << std::fixed << std::setprecision(2) << avg << "\n\n";

        f << "──────── DEFECT DETECTION ────────\n";
        f << "OK pixels      : " << ok_pixels << "\n";
        f << "DEFECT pixels  : " << defects   << "\n";
        f << "Defect rate    : " << std::fixed << std::setprecision(2) << defect_pct << "%\n\n";

        f << "──────── CONNECTED COMPONENTS ────────\n";
        f << "Total components found: " << m_components.size() << "\n";
        for (const auto& c : m_components)
            f << "  label=" << static_cast<int>(c.label)
              << "  pixels=" << c.pixel_count
              << "  bbox=(" << c.row_start << "," << c.col_start
              << ")->(" << c.row_end << "," << c.col_end << ")\n";

        f << "\n──────── TIMING ────────\n";
        f << "Worst DataGen cycle   : " << m_data_gen->worst_cycle_ns() << " ns\n";
        f << "Worst inter-output gap: " << m_filter->worst_latency_ns() << " ns\n";
        f << "Throughput (<100ns)   : "
          << (m_filter->worst_latency_ns() <= m_cfg.interval_ns ? "PASS" : "FAIL") << "\n\n";

        f << "──────── CONCLUSION ────────\n";
        if (defect_pct < 5.0)
            f << "SURFACE QUALITY: GOOD\n";
        else if (defect_pct < 20.0)
            f << "SURFACE QUALITY: MODERATE\n";
        else
            f << "SURFACE QUALITY: POOR\n";
        f << "==========================================================\n";
    }

    // Console summary
    std::cout << "\n==========================================================\n";
    std::cout << "  Results saved to: " << run_dir << "/\n";
    std::cout << "==========================================================\n";
    std::cout << "  results.csv          <- all " << m_outputs.size() << " pixels\n";
    std::cout << "  defects_only.csv     <- " << defects << " defect pixels\n";
    std::cout << "  components.csv       <- " << m_components.size() << " components\n";
    std::cout << "  inference_report.txt <- full analysis\n";
    std::cout << "----------------------------------------------------------\n";
    std::cout << "  DEFECT RATE  : " << std::fixed << std::setprecision(2)
              << defect_pct << "% (" << defects << "/" << m_outputs.size() << ")\n";
    std::cout << "  COMPONENTS   : " << m_components.size() << " blobs found\n";
    if (defect_pct < 5.0)       std::cout << "  VERDICT: GOOD SURFACE\n";
    else if (defect_pct < 20.0) std::cout << "  VERDICT: MODERATE\n";
    else                         std::cout << "  VERDICT: POOR\n";
    std::cout << "==========================================================\n";
}

// ----------------------------------------------------------------------------
// Prints a concise performance profile to the console.
// ----------------------------------------------------------------------------
void Pipeline::print_profile() const
{
    int defects = 0;
    for (const auto& o : m_outputs) if (o.thresholded == 1) ++defects;
    double pct = m_outputs.empty() ? 0.0 : 100.0 * defects / m_outputs.size();

    std::cout << "\n========== Pipeline Profile ==========\n"
              << "  Pairs emitted   : " << m_data_gen->pairs_emitted()   << "\n"
              << "  Pixels processed: " << m_data_gen->pairs_emitted()*2 << "\n"
              << "  Worst DataGen ns: " << m_data_gen->worst_cycle_ns()  << "\n"
              << "  Pixels output   : " << m_filter->pixels_output()     << "\n"
              << "  Worst gap ns    : " << m_filter->worst_latency_ns()  << "\n"
              << "  Throughput      : "
              << (m_filter->worst_latency_ns() <= m_cfg.interval_ns ? "PASS" : "FAIL") << "\n"
              << "  OK pixels       : " << m_outputs.size() - defects    << "\n"
              << "  DEFECT pixels   : " << defects                       << "\n"
              << "  Defect rate     : " << std::fixed << std::setprecision(2) << pct << "%\n"
              << "  Components found: " << m_components.size()           << "\n"
              << "======================================\n\n";
}