#pragma once
#include "BoundedQueue.h"
#include "FilterThresholdBlock.h"
#include <memory>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <cstdint>
#include <mutex>

// Output from LabellingBlock to TracingBlock
struct LabelledPixel {
    uint8_t  label{0};       // 0 = background
    uint8_t  value{0};       // original thresholded value
    bool     merge_occurred{false};
    uint8_t  old_label{0};   // label being replaced
    uint8_t  new_label{0};   // label it merges into
};

class LabellingBlock {
public:
    using OutputCallback = std::function<void(const LabelledPixel&)>;

    LabellingBlock(std::shared_ptr<BoundedQueue<FilteredOutput>> input_queue,
                   int columns,
                   OutputCallback on_output = nullptr);
    ~LabellingBlock();

    void start();
    void stop();
    void recycle_label(uint8_t label);  // called back by TracingBlock

private:
    void run();
    uint8_t next_free_label();
    uint8_t find_root(uint8_t label);
    void    union_labels(uint8_t a, uint8_t b);

    std::shared_ptr<BoundedQueue<FilteredOutput>> m_input;
    int            m_columns;
    OutputCallback m_on_output;

    // 1 row of history only (assignment constraint: uint16 datatype → use uint16_t)
    std::vector<uint16_t> m_prev_row;   // previous row labels
    std::vector<uint16_t> m_curr_row;   // current row being filled

    // Union-Find: 2 arrays of max size m/2 (assignment constraint)
    std::vector<uint8_t>  m_parent;     // array 1: union-find parent
    std::vector<uint8_t>  m_rank;       // array 2: union-find rank
    int      m_max_labels;              // = m/2

    // Label pool
    std::vector<uint8_t>  m_free_labels;
    uint8_t               m_next_label{1};
    std::mutex            m_recycle_mutex;

    std::thread        m_thread;
    std::atomic<bool>  m_running{false};
    int                m_col_idx{0};
};
