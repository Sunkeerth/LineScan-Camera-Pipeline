#include "TracingBlock.h"
#include <limits>
#include <iostream>

TracingBlock::TracingBlock(
    std::shared_ptr<BoundedQueue<LabelledPixel>> input_queue,
    int columns,
    int max_labels,
    DoneCallback    on_done,
    RecycleCallback on_recycle)
    : m_input(std::move(input_queue))
    , m_columns(columns)
    , m_max_labels(max_labels)
    , m_on_done(std::move(on_done))
    , m_on_recycle(std::move(on_recycle))
    , m_count    (max_labels + 1, 0)
    , m_row_start(max_labels + 1, std::numeric_limits<int>::max())
    , m_col_start(max_labels + 1, std::numeric_limits<int>::max())
    , m_row_end  (max_labels + 1, 0)
    , m_col_end  (max_labels + 1, 0)
    , m_active   (max_labels + 1, false)
{}

TracingBlock::~TracingBlock() { stop(); }

void TracingBlock::start() {
    m_running = true;
    m_thread  = std::thread(&TracingBlock::run, this);
}

void TracingBlock::stop() {
    m_running = false;
    m_input->close();
    if (m_thread.joinable()) m_thread.join();
}

void TracingBlock::finalize_label(uint8_t label) {
    if (!m_active[label] || m_count[label] == 0) return;
    ComponentResult r;
    r.label       = label;
    r.pixel_count = m_count[label];
    r.row_start   = m_row_start[label];
    r.col_start   = m_col_start[label];
    r.row_end     = m_row_end[label];
    r.col_end     = m_col_end[label];
    if (m_on_done) m_on_done(r);
    // Reset stats
    m_count[label]     = 0;
    m_row_start[label] = std::numeric_limits<int>::max();
    m_col_start[label] = std::numeric_limits<int>::max();
    m_row_end[label]   = 0;
    m_col_end[label]   = 0;
    m_active[label]    = false;
    // Recycle label back to LabellingBlock
    if (m_on_recycle) m_on_recycle(label);
}

void TracingBlock::run() {
    while (m_running) {
        auto maybe = m_input->pop();
        if (!maybe.has_value()) break;
        const LabelledPixel& lp = maybe.value();

        if (lp.merge_occurred) {
            // Absorb old_label stats into new_label
            uint8_t ol = lp.old_label, nl = lp.new_label;
            if (ol > 0 && nl > 0 && ol <= m_max_labels && nl <= m_max_labels) {
                m_count[nl]     += m_count[ol];
                m_row_start[nl]  = std::min(m_row_start[nl], m_row_start[ol]);
                m_col_start[nl]  = std::min(m_col_start[nl], m_col_start[ol]);
                m_row_end[nl]    = std::max(m_row_end[nl],   m_row_end[ol]);
                m_col_end[nl]    = std::max(m_col_end[nl],   m_col_end[ol]);
                m_active[nl]     = true;
                // Reset old label (not finalized — just absorbed)
                m_count[ol]      = 0;
                m_row_start[ol]  = std::numeric_limits<int>::max();
                m_col_start[ol]  = std::numeric_limits<int>::max();
                m_row_end[ol]    = 0;
                m_col_end[ol]    = 0;
                m_active[ol]     = false;
                if (m_on_recycle) m_on_recycle(ol);
            }
            continue;
        }

        // Normal pixel
        if (lp.label != 0 && lp.label <= static_cast<uint8_t>(m_max_labels)) {
            uint8_t lb = lp.label;
            ++m_count[lb];
            m_active[lb]    = true;
            m_row_start[lb] = std::min(m_row_start[lb], m_current_row);
            m_col_start[lb] = std::min(m_col_start[lb], m_col_idx);
            m_row_end[lb]   = std::max(m_row_end[lb],   m_current_row);
            m_col_end[lb]   = std::max(m_col_end[lb],   m_col_idx);
        }

        ++m_col_idx;
        if (m_col_idx >= m_columns) {
            m_col_idx = 0;
            ++m_current_row;
        }
    }
    // Flush all remaining active labels at end of stream
    for (int i = 1; i <= m_max_labels; ++i)
        finalize_label(static_cast<uint8_t>(i));
}
