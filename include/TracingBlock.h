#pragma once
#include "BoundedQueue.h"
#include "LabellingBlock.h"
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <cstdint>

// Final output: one completed connected component
struct ComponentResult {
    uint8_t  label{0};
    uint32_t pixel_count{0};
    int      row_start{0}, col_start{0};
    int      row_end{0},   col_end{0};
};

class TracingBlock {
public:
    using DoneCallback    = std::function<void(const ComponentResult&)>;
    using RecycleCallback = std::function<void(uint8_t)>;

    TracingBlock(std::shared_ptr<BoundedQueue<LabelledPixel>> input_queue,
                 int columns,
                 int max_labels,
                 DoneCallback    on_done,
                 RecycleCallback on_recycle);
    ~TracingBlock();

    void start();
    void stop();

private:
    void run();
    void finalize_label(uint8_t label);

    std::shared_ptr<BoundedQueue<LabelledPixel>> m_input;
    int m_columns;
    int m_max_labels;
    DoneCallback    m_on_done;
    RecycleCallback m_on_recycle;

    std::vector<uint32_t> m_count;
    std::vector<int>      m_row_start, m_col_start;
    std::vector<int>      m_row_end,   m_col_end;
    std::vector<bool>     m_active;    // is this label currently alive?

    std::thread       m_thread;
    std::atomic<bool> m_running{false};
    int               m_current_row{0};
    int               m_col_idx{0};
};
