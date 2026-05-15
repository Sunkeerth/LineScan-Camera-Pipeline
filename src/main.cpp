// ============================================================================
// main.cpp
// Entry point for the CynLr line‑scan camera pipeline.
// Parses command line arguments, configures the pipeline, and runs it.
// Supports three sources: webcam (default, /dev/video2), CSV test mode, or RNG.
// Results are saved to ../data/run_<timestamp>/.
// ============================================================================

// Include the Pipeline class definition (contains all block management).
#include "Pipeline.h"

// Standard C++ library headers for I/O, string handling, exceptions,
// and signal handling.
#include <iostream>      // std::cout, std::cerr for console output.
#include <string>        // std::string for command line argument parsing.
#include <stdexcept>     // std::exception for catching errors.
#include <csignal>       // std::signal, SIGINT, SIGTERM for graceful shutdown.

// ----------------------------------------------------------------------------
// Global pointer to the Pipeline instance. Used by the signal handler to
// request a stop when Ctrl+C is pressed. Must be static to have internal
// linkage (only visible in this translation unit).
// ----------------------------------------------------------------------------
static Pipeline* g_pipeline = nullptr;

// ----------------------------------------------------------------------------
// Signal handler function for SIGINT (Ctrl+C) and SIGTERM (termination signal).
// Parameter 'int' is the signal number (ignored here). The function prints a
// message and calls request_stop() on the pipeline, which triggers an orderly
// shutdown of all blocks and saves results.
// ----------------------------------------------------------------------------
static void on_ctrl_c(int)
{
    // Print a message to indicate that shutdown has started.
    std::cout << "\n[Ctrl+C] Stopping — saving results to data/ folder...\n";
    // If the global pipeline pointer is valid, request it to stop.
    if (g_pipeline) g_pipeline->request_stop();
}

// ----------------------------------------------------------------------------
// main() – program entry point.
// argc: number of command line arguments (including program name).
// argv: array of C‑strings containing the arguments.
// Returns 0 on success, non‑zero on error.
// ----------------------------------------------------------------------------
int main(int argc, char* argv[])
{
    // If less than 2 arguments (i.e., only the program name), print usage help.
    if (argc < 2)
    {
        std::cout
            << "\n============================================================\n"
            << "  CynLr Evaluation 1 — Line-Scan Camera Pipeline\n"
            << "  External USB Webcam: /dev/video2\n"
            << "  Results auto-saved: CynLr_Final/data/run_<timestamp>/\n"
            << "============================================================\n\n"
            << "Usage: " << argv[0]   // program name
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
        return 1;   // Exit with error because no valid configuration provided.
    }

    // Create a PipelineConfig structure (defined in Pipeline.h) with default
    // values (e.g., columns=8, threshold=100, interval_ns=500, etc.).
    PipelineConfig cfg;

    // ------------------------------------------------------------------------
    // Parse command line arguments. Iterate over each argument starting from
    // index 1 (skip program name). Use a simple if‑else chain.
    // ------------------------------------------------------------------------
    for (int i = 1; i < argc; ++i)
    {
        // Convert current argument to std::string for easier comparison.
        std::string a = argv[i];

        // Check for known flags. When a flag consumes the next argument,
        // we increment 'i' to skip it.
        if      (a == "-m" && i+1 < argc)
            cfg.columns = std::stoi(argv[++i]);   // store columns count (must be even)
        else if (a == "-tv" && i+1 < argc)
            cfg.threshold_tv = std::stod(argv[++i]); // threshold value (double)
        else if (a == "-t" && i+1 < argc)
            cfg.interval_ns = std::stoll(argv[++i]); // interval in nanoseconds
        else if (a == "-csv" && i+1 < argc)
        {
            cfg.csv_path = argv[++i];    // path to CSV file
            cfg.test_mode = true;        // enable test mode (CSV source)
        }
        else if (a == "-pairs" && i+1 < argc)
            cfg.max_pairs = std::stoull(argv[++i]); // auto‑stop after N pairs
        else if (a == "-verbose")
            cfg.verbose = true;           // print every pixel's details live
        else if (a == "-webcam")
            cfg.webcam_mode = true;       // use webcam as source
        else if (a == "-cam" && i+1 < argc)
            cfg.webcam_device = std::stoi(argv[++i]); // V4L2 device index (e.g., 2)
        else if (a == "-h" || a == "--help")
        {
            std::cout << "Run without args to see full help.\n";
            return 0;   // help requested, exit successfully.
        }
        else
        {
            std::cerr << "Unknown: " << a << "\n";
            return 1;   // unknown argument → error.
        }
    }

    // After parsing, ensure that columns have been set (required parameter).
    if (cfg.columns <= 0)
    {
        std::cerr << "Error: -m required\n";
        return 1;
    }

    // ------------------------------------------------------------------------
    // Print the configuration summary to the console so the user can verify
    // the selected settings before the pipeline starts.
    // ------------------------------------------------------------------------
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
        std::cout << "RNG\n";    // default random number generator

    std::cout << "  Columns m  : " << cfg.columns       << "\n"
              << "  Threshold  : " << cfg.threshold_tv  << "\n"
              << "  Interval T : " << cfg.interval_ns   << " ns\n";

    if (cfg.max_pairs > 0)
        std::cout << "  Max pairs  : " << cfg.max_pairs << "\n";
    else
        std::cout << "  Running    : until Ctrl+C\n";

    std::cout << "  Saving to  : CynLr_Final/data/run_<timestamp>/\n"
              << "============================================================\n\n";

    // ------------------------------------------------------------------------
    // Try to construct and run the pipeline. Catch any exceptions that might
    // be thrown during construction (e.g., invalid parameters, camera error).
    // ------------------------------------------------------------------------
    try
    {
        // Create the Pipeline object with the parsed configuration.
        // This constructor creates all queues, instantiates the data source,
        // and builds the four blocks (DataGeneration, FilterThreshold,
        // Labelling, Tracing).
        Pipeline pipeline(cfg);

        // Store the address of the pipeline in the global pointer so that
        // the signal handler can access it.
        g_pipeline = &pipeline;

        // Register the signal handler for SIGINT (Ctrl+C) and SIGTERM
        // (termination signal sent by e.g., kill command).
        std::signal(SIGINT,  on_ctrl_c);
        std::signal(SIGTERM, on_ctrl_c);

        // Run the pipeline: this starts all four threads, waits until
        // data generation finishes (either source exhausted or Ctrl+C),
        // then shuts down and saves results.
        pipeline.run();

        // After run() returns, print a performance profile summary
        // (pixel counts, worst latencies, defect rate, etc.).
        pipeline.print_profile();
    }
    catch (const std::exception& ex)
    {
        // If any error occurs during construction or execution, print
        // the error message and return with failure code.
        std::cerr << "\nFatal error: " << ex.what() << "\n";
        return 1;
    }

    // Success.
    return 0;
}