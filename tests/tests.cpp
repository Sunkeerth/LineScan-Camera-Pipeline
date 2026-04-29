/**
 * @file tests.cpp
 * @brief Unit tests for CynLr Evaluation 1
 */
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>

#include "CsvDataSource.h"
#include "RngDataSource.h"
#include "BoundedQueue.h"
#include "FilterThresholdBlock.h"
#include "Pipeline.h"

static int pass_count = 0;
static int fail_count = 0;

#define TEST(name, expr) \
    do { if (expr) { std::cout << "[PASS] " << name << "\n"; ++pass_count; } \
         else       { std::cout << "[FAIL] " << name << "\n"; ++fail_count; } } while(0)

static void write_csv(const std::string& path,
                      const std::vector<std::vector<int>>& rows)
{
    std::ofstream f(path);
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            f << row[i];
            if (i + 1 < row.size()) f << ",";
        }
        f << "\n";
    }
}

// ── CsvDataSource ────────────────────────────────────────────────────────────
void test_csv_source()
{
    const std::string path = "/tmp/cynlr_test.csv";
    write_csv(path, {{10, 20, 30, 40}, {50, 60, 70, 80}});

    CsvDataSource src(path, 4);
    uint8_t a, b;

    TEST("CSV pair1 ok",       src.next(a, b));
    TEST("CSV pair1 p1=10",    a == 10);
    TEST("CSV pair1 p2=20",    b == 20);
    src.next(a, b);
    TEST("CSV pair2 p1=30",    a == 30);
    src.next(a, b); src.next(a, b);
    TEST("CSV exhausted",      !src.next(a, b));
    src.reset(); src.next(a, b);
    TEST("CSV reset restarts", a == 10 && b == 20);
}

// ── RngDataSource ────────────────────────────────────────────────────────────
void test_rng_source()
{
    RngDataSource src;
    uint8_t a, b;
    bool all_ok = true;
    for (int i = 0; i < 1000; ++i)
        if (!src.next(a, b)) { all_ok = false; break; }
    TEST("RNG always returns true", all_ok);
}

// ── BoundedQueue ─────────────────────────────────────────────────────────────
void test_bounded_queue()
{
    BoundedQueue<int> q(3);
    q.push(1); q.push(2); q.push(3);
    auto v = q.pop();
    TEST("Queue FIFO",                    v.has_value() && v.value() == 1);
    q.close();
    auto v2 = q.pop(); TEST("Drain gives 2",   v2.has_value() && v2.value() == 2);
    auto v3 = q.pop(); TEST("Drain gives 3",   v3.has_value() && v3.value() == 3);
    auto v4 = q.pop(); TEST("Closed+empty nullopt", !v4.has_value());
}

// ── Filter convolution ───────────────────────────────────────────────────────
void test_filter()
{
    double sum = 0.0;
    for (double w : FilterThresholdBlock::FILTER_WINDOW) sum += w;
    TEST("Filter window sums to ~1.0", std::fabs(sum - 1.0) < 1e-3);

    double result = 0.0;
    for (int i = 0; i < 9; ++i)
        result += 200.0 * FilterThresholdBlock::FILTER_WINDOW[i];
    TEST("Constant signal convolution = identity", std::fabs(result - 200.0) < 0.1);
}

// ── End-to-end pipeline (CSV) ────────────────────────────────────────────────
void test_pipeline_csv()
{
    const std::string path = "/tmp/cynlr_integ.csv";
    std::vector<std::vector<int>> data;
    for (int r = 0; r < 4; ++r) {
        std::vector<int> row;
        for (int c = 0; c < 8; ++c) row.push_back((r * 8 + c) % 256);
        data.push_back(row);
    }
    write_csv(path, data);

    PipelineConfig cfg;
    cfg.columns = 8; cfg.threshold_tv = 10.0; cfg.interval_ns = 500;
    cfg.test_mode = true; cfg.csv_path = path; cfg.verbose = false;

    Pipeline pl(cfg);
    pl.run();

    bool all_binary = true;
    for (const auto& o : pl.outputs())
        if (o.thresholded != 0 && o.thresholded != 1) all_binary = false;

    TEST("All thresholded values are 0 or 1", all_binary);
    TEST("Pipeline produced pixels",           pl.outputs().size() > 0);
    std::cout << "  (pixels produced: " << pl.outputs().size() << ")\n";
}

int main()
{
    std::cout << "===== CynLr Eval1 Unit Tests =====\n\n";
    test_csv_source();
    std::cout << "\n";
    test_rng_source();
    std::cout << "\n";
    test_bounded_queue();
    std::cout << "\n";
    test_filter();
    std::cout << "\n";
    test_pipeline_csv();
    std::cout << "\n----------------------------------\n";
    std::cout << "PASSED: " << pass_count << "  FAILED: " << fail_count << "\n";
    return fail_count > 0 ? 1 : 0;
}
