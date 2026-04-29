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
#include <sys/stat.h>

// ── Timestamp string for unique run folder names ─────────────────────────────
static std::string make_timestamp()
{
    auto now  = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    return std::string(buf);
}

static void mkdir_p(const std::string& path)
{
    // Create directory (ignore if already exists)
    mkdir(path.c_str(), 0755);
}

Pipeline::Pipeline(PipelineConfig cfg) : m_cfg(std::move(cfg))
{
    if (m_cfg.interval_ns < 100)
        throw std::invalid_argument("Interval T must be >= 100 ns.");
    if (m_cfg.columns <= 0 || m_cfg.columns % 2 != 0)
        throw std::invalid_argument("Columns m must be a positive even number.");

    // Generate unique run ID (used for subfolder + filenames)
    m_run_id = make_timestamp();

    m_queue = std::make_shared<BoundedQueue<PixelPair>>(
        static_cast<size_t>(m_cfg.columns));

    // Webcam preview saved inside data/run_<id>/
    std::string run_dir = m_cfg.project_data_dir + "/run_" + m_run_id;
    std::string preview_path = run_dir + "/webcam_frame.png";

    std::unique_ptr<IDataSource> source;
    if (m_cfg.webcam_mode)
    {
        // Create run dir before capture so preview can be saved
        mkdir_p(m_cfg.project_data_dir);
        mkdir_p(run_dir);
        source = std::make_unique<WebcamDataSource>(
            m_cfg.columns, m_cfg.webcam_rows,
            m_cfg.webcam_device, preview_path);
    }
    else if (m_cfg.test_mode)
        source = std::make_unique<CsvDataSource>(m_cfg.csv_path, m_cfg.columns);
    else
        source = std::make_unique<RngDataSource>();

    m_data_gen = std::make_unique<DataGenerationBlock>(
        std::move(source), m_queue, m_cfg.interval_ns, m_cfg.max_pairs);

    m_output_mutex = std::make_unique<std::mutex>();
    std::mutex* mtx = m_output_mutex.get();

    auto on_output = [this, mtx](const FilteredOutput& out)
    {
        std::lock_guard<std::mutex> lk(*mtx);
        if (m_cfg.verbose)
            std::cout << "pixel=" << std::setw(3) << static_cast<int>(out.original)
                      << "  filtered=" << std::fixed << std::setprecision(4)
                      << out.filtered
                      << "  result=" << (out.thresholded ? "DEFECT" : "ok") << "\n";
        m_outputs.push_back(out);
    };

    m_filter = std::make_unique<FilterThresholdBlock>(
        m_queue, m_cfg.threshold_tv, on_output);
}

void Pipeline::run()
{
    m_filter->start();
    m_data_gen->start();
    m_data_gen->wait_until_done();   // blocks until source exhausted or Ctrl+C
    m_filter->stop();
    save_results();                  // auto-save everything to data/run_<id>/
}

void Pipeline::request_stop() { m_data_gen->stop(); }

void Pipeline::save_results() const
{
    std::string run_dir = m_cfg.project_data_dir + "/run_" + m_run_id;
    mkdir_p(m_cfg.project_data_dir);
    mkdir_p(run_dir);

    // ── Compute statistics ───────────────────────────────────────────────────
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
    double avg = m_outputs.empty() ? 0.0 : sum / m_outputs.size();
    double defect_pct = m_outputs.empty() ? 0.0
        : (100.0 * defects / m_outputs.size());

    // ── 1. Save ALL pixel results to CSV ────────────────────────────────────
    std::string csv_path = run_dir + "/results.csv";
    {
        std::ofstream f(csv_path);
        f << "pixel_index,raw_value(0-255),filtered_value,defect(1=DEFECT_0=ok)\n";
        for (size_t i = 0; i < m_outputs.size(); ++i)
            f << i << ","
              << static_cast<int>(m_outputs[i].original) << ","
              << std::fixed << std::setprecision(6) << m_outputs[i].filtered << ","
              << static_cast<int>(m_outputs[i].thresholded) << "\n";
    }

    // ── 2. Save defects-only CSV (quick reference) ───────────────────────────
    std::string defects_path = run_dir + "/defects_only.csv";
    {
        std::ofstream f(defects_path);
        f << "pixel_index,raw_value,filtered_value\n";
        for (size_t i = 0; i < m_outputs.size(); ++i)
            if (m_outputs[i].thresholded == 1)
                f << i << ","
                  << static_cast<int>(m_outputs[i].original) << ","
                  << std::fixed << std::setprecision(6)
                  << m_outputs[i].filtered << "\n";
    }

    // ── 3. Save inference report (human-readable analysis) ───────────────────
    std::string rpt_path = run_dir + "/inference_report.txt";
    {
        std::ofstream f(rpt_path);
        f << "==========================================================\n";
        f << "   CynLr Pipeline — Inference Report\n";
        f << "   Run ID : " << m_run_id << "\n";
        f << "==========================================================\n\n";

        f << "SOURCE         : ";
        if (m_cfg.webcam_mode)
            f << "External USB Webcam (/dev/video" << m_cfg.webcam_device << ")\n";
        else if (m_cfg.test_mode)
            f << "CSV (" << m_cfg.csv_path << ")\n";
        else
            f << "RNG (random)\n";

        f << "COLUMNS (m)    : " << m_cfg.columns      << "\n";
        f << "THRESHOLD (TV) : " << m_cfg.threshold_tv << "\n";
        f << "INTERVAL T     : " << m_cfg.interval_ns  << " ns\n\n";

        f << "──────── WHAT THE CAMERA SAW ────────\n";
        if (m_cfg.webcam_mode)
        {
            f << "Captured frame : " << run_dir << "/webcam_frame.png\n";
            f << "Frame size     : " << m_cfg.webcam_rows << " rows x "
              << m_cfg.columns << " cols\n";
        }
        f << "Total pixels   : " << m_outputs.size() << "\n";
        f << "Total pairs    : " << m_data_gen->pairs_emitted() << "\n\n";

        f << "──────── PIXEL STATISTICS ────────\n";
        f << "Min pixel value   : " << min_val << " (dark)\n";
        f << "Max pixel value   : " << max_val << " (bright)\n";
        f << "Average value     : " << std::fixed << std::setprecision(2) << avg << "\n\n";

        f << "──────── DEFECT DETECTION ────────\n";
        f << "OK pixels (bin=0) : " << ok_pixels << "\n";
        f << "DEFECT    (bin=1) : " << defects   << "\n";
        f << "Defect rate       : " << std::fixed << std::setprecision(2)
          << defect_pct << "%\n\n";

        f << "──────── TIMING ────────\n";
        f << "Worst DataGen cycle   : " << m_data_gen->worst_cycle_ns() << " ns\n";
        f << "Worst inter-output gap: " << m_filter->worst_latency_ns() << " ns\n";
        f << "Throughput (<= T ns)  : "
          << (m_filter->worst_latency_ns() <= m_cfg.interval_ns ? "PASS" : "FAIL") << "\n\n";

        f << "──────── CONCLUSION ────────\n";
        if (defect_pct < 5.0)
            f << "SURFACE QUALITY: GOOD    — defect rate "
              << std::fixed<<std::setprecision(2) << defect_pct << "%\n";
        else if (defect_pct < 20.0)
            f << "SURFACE QUALITY: MODERATE — defect rate "
              << std::fixed<<std::setprecision(2) << defect_pct << "%\n";
        else
            f << "SURFACE QUALITY: POOR    — defect rate "
              << std::fixed<<std::setprecision(2) << defect_pct << "%\n";

        f << "\n──────── FILES SAVED ────────\n";
        f << run_dir << "/webcam_frame.png    <- what camera saw\n";
        f << run_dir << "/results.csv         <- all pixels\n";
        f << run_dir << "/defects_only.csv    <- only defect pixels\n";
        f << run_dir << "/inference_report.txt<- this report\n";
        f << "==========================================================\n";
    }

    // ── Print to terminal ────────────────────────────────────────────────────
    std::cout << "\n==========================================================\n";
    std::cout << "  Results saved to: " << run_dir << "/\n";
    std::cout << "==========================================================\n";
    std::cout << "  webcam_frame.png     <- what the camera captured\n";
    std::cout << "  results.csv          <- all " << m_outputs.size() << " pixels\n";
    std::cout << "  defects_only.csv     <- " << defects << " defect pixels\n";
    std::cout << "  inference_report.txt <- full analysis\n";
    std::cout << "----------------------------------------------------------\n";
    std::cout << "  DEFECT RATE: " << std::fixed<<std::setprecision(2)
              << defect_pct << "% (" << defects << " / " << m_outputs.size() << " pixels)\n";
    if (defect_pct < 5.0)
        std::cout << "  VERDICT: GOOD SURFACE\n";
    else if (defect_pct < 20.0)
        std::cout << "  VERDICT: MODERATE — some defects found\n";
    else
        std::cout << "  VERDICT: POOR — high defect rate\n";
    std::cout << "==========================================================\n";
}

void Pipeline::print_profile() const
{
    int defects = 0;
    for (const auto& o : m_outputs) if (o.thresholded == 1) ++defects;
    double pct = m_outputs.empty() ? 0.0 : 100.0*defects/m_outputs.size();

    std::cout << "\n========== Pipeline Profile ==========\n"
              << "  Pairs emitted   : " << m_data_gen->pairs_emitted()   << "\n"
              << "  Pixels processed: " << m_data_gen->pairs_emitted()*2 << "\n"
              << "  Worst DataGen ns: " << m_data_gen->worst_cycle_ns()  << "\n"
              << "  Pixels output   : " << m_filter->pixels_output()     << "\n"
              << "  Worst gap ns    : " << m_filter->worst_latency_ns()  << "\n"
              << "  Throughput      : "
              << (m_filter->worst_latency_ns()<=m_cfg.interval_ns?"PASS":"FAIL") << "\n"
              << "  OK pixels       : " << m_outputs.size()-defects      << "\n"
              << "  DEFECT pixels   : " << defects                       << "\n"
              << "  Defect rate     : " << std::fixed<<std::setprecision(2)
              << pct << "%\n"
              << "======================================\n\n";
}