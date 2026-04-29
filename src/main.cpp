/**
 * CynLr Evaluation 1 — Entry Point
 * External USB webcam /dev/video2 + real-time processing
 * All results saved to project data/ folder automatically
 */
#include "Pipeline.h"
#include <iostream>
#include <string>
#include <stdexcept>
#include <csignal>

static Pipeline* g_pipeline = nullptr;
static void on_ctrl_c(int)
{
    std::cout << "\n[Ctrl+C] Stopping — saving results to data/ folder...\n";
    if (g_pipeline) g_pipeline->request_stop();
}

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout
            << "\n============================================================\n"
            << "  CynLr Evaluation 1 — Line-Scan Camera Pipeline\n"
            << "  External USB Webcam: /dev/video2\n"
            << "  Results auto-saved: CynLr_Final/data/run_<timestamp>/\n"
            << "============================================================\n\n"
            << "Usage: " << argv[0]
            << " -m <cols> -tv <threshold> -t <ns> [source] [options]\n\n"
            << "  -m  <cols>    Even pixel columns        e.g. 640\n"
            << "  -tv <val>     Threshold TV              e.g. 128\n"
            << "  -t  <ns>      Interval nanoseconds      e.g. 500\n\n"
            << "Source:\n"
            << "  -webcam       External USB webcam /dev/video2\n"
            << "                Runs CONTINUOUSLY until Ctrl+C\n"
            << "  -csv <path>   CSV test mode\n"
            << "  (default)     RNG random mode\n\n"
            << "Options:\n"
            << "  -pairs <n>    Auto-stop after N pairs\n"
            << "  -verbose      Print every pixel live\n\n"
            << "Examples:\n"
            << "  " << argv[0] << " -m 640 -tv 128 -t 500 -webcam\n"
            << "  " << argv[0] << " -m 640 -tv 128 -t 500 -webcam -pairs 5000\n"
            << "  " << argv[0] << " -m 640 -tv 128 -t 500 -webcam -verbose\n"
            << "  " << argv[0] << " -m 8 -tv 80 -t 500 -csv ../data/sample.csv\n\n"
            << "After run, check results:\n"
            << "  ls ../data/\n"
            << "  cat ../data/run_<timestamp>/inference_report.txt\n\n";
        return 1;
    }

    PipelineConfig cfg;

    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        if      (a=="-m"     && i+1<argc) cfg.columns       = std::stoi(argv[++i]);
        else if (a=="-tv"    && i+1<argc) cfg.threshold_tv  = std::stod(argv[++i]);
        else if (a=="-t"     && i+1<argc) cfg.interval_ns   = std::stoll(argv[++i]);
        else if (a=="-csv"   && i+1<argc) { cfg.csv_path=argv[++i]; cfg.test_mode=true; }
        else if (a=="-pairs" && i+1<argc) cfg.max_pairs     = std::stoull(argv[++i]);
        else if (a=="-verbose")           cfg.verbose        = true;
        else if (a=="-webcam")            cfg.webcam_mode    = true;
        else if (a=="-cam"   && i+1<argc) cfg.webcam_device = std::stoi(argv[++i]);
        else if (a=="-h" || a=="--help")
        {
            std::cout << "Run without args to see full help.\n";
            return 0;
        }
        else { std::cerr << "Unknown: " << a << "\n"; return 1; }
    }

    if (cfg.columns <= 0) { std::cerr << "Error: -m required\n"; return 1; }

    std::cout << "\n============================================================\n"
              << "  CynLr Evaluation 1\n"
              << "============================================================\n"
              << "  Source     : ";
    if (cfg.webcam_mode)
        std::cout << "External USB Webcam (/dev/video"
                  << cfg.webcam_device << ")\n";
    else if (cfg.test_mode)
        std::cout << "CSV (" << cfg.csv_path << ")\n";
    else
        std::cout << "RNG\n";

    std::cout << "  Columns m  : " << cfg.columns       << "\n"
              << "  Threshold  : " << cfg.threshold_tv  << "\n"
              << "  Interval T : " << cfg.interval_ns   << " ns\n";

    if (cfg.max_pairs > 0)
        std::cout << "  Max pairs  : " << cfg.max_pairs << "\n";
    else
        std::cout << "  Running    : until Ctrl+C\n";

    std::cout << "  Saving to  : CynLr_Final/data/run_<timestamp>/\n"
              << "============================================================\n\n";

    try
    {
        Pipeline pipeline(cfg);
        g_pipeline = &pipeline;
        std::signal(SIGINT,  on_ctrl_c);
        std::signal(SIGTERM, on_ctrl_c);

        pipeline.run();
        pipeline.print_profile();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "\nFatal error: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}