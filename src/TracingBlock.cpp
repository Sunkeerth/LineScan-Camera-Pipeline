// ============================================================================
// TracingBlock.cpp
// Implementation of Block 4 – Tracing / Statistics.
// Receives LabelledPixel from Block 3. For each label, maintains:
//   - pixel count
//   - bounding box (min/max row, min/max column)
// When a merge event arrives, it absorbs the stats of the old label into the new one.
// When a label can never receive more pixels (end of stream or after a full row of zeros),
// it finalizes the blob, outputs ComponentResult, and recycles the label.
// ============================================================================

#include "TracingBlock.h"
#include <limits>   // std::numeric_limits
#include <iostream>

// ----------------------------------------------------------------------------
// Constructor: initialises per‑label arrays with appropriate default values.
// m_max_labels = columns/2 (maximum active labels at once).
// Arrays are sized max_labels+1 (index 0 unused).
// ----------------------------------------------------------------------------
TracingBlock::TracingBlock(
    std::shared_ptr<BoundedQueue<LabelledPixel>> input_queue,  // from Block 3
    int columns,                                                // row width (m)
    int max_labels,                                             // = m/2
    DoneCallback    on_done,                                    // called when blob finished
    RecycleCallback on_recycle)                                 // called to return label to Block 3
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

// ----------------------------------------------------------------------------
// Finalise a label: output ComponentResult, reset stats, and recycle the label.
// Called when the blob is complete (either because a full row passed with no pixel
// of that label, or at the end of the stream).
// ----------------------------------------------------------------------------
void TracingBlock::finalize_label(uint8_t label) {
    if (!m_active[label] || m_count[label] == 0) return;
    ComponentResult r;
    r.label       = label;
    r.pixel_count = m_count[label];
    r.row_start   = m_row_start[label];
    r.col_start   = m_col_start[label];
    r.row_end     = m_row_end[label];
    r.col_end     = m_col_end[label];
    if (m_on_done) m_on_done(r);            // send to Pipeline

    // Reset statistics for this label
    m_count[label]     = 0;
    m_row_start[label] = std::numeric_limits<int>::max();
    m_col_start[label] = std::numeric_limits<int>::max();
    m_row_end[label]   = 0;
    m_col_end[label]   = 0;
    m_active[label]    = false;

    // Return label number to Block 3 for reuse (label recycling)
    if (m_on_recycle) m_on_recycle(label);
}

// ----------------------------------------------------------------------------
// Main loop: processes LabelledPixel messages.
// Two types of messages:
//   1. merge_occurred = true : absorb stats of old_label into new_label
//   2. normal pixel : update statistics for the label at the current row/col
// At end of each row, no automatic finalization – instead we rely on the fact
// that Block 3 resets union‑find every row; any label not seen in the next row
// will never reappear. Finalization happens only at the end of the stream.
// ----------------------------------------------------------------------------
void TracingBlock::run() {
    while (m_running) {
        auto maybe = m_input->pop();
        if (!maybe.has_value()) break;
        const LabelledPixel& lp = maybe.value();

        if (lp.merge_occurred) {
            // Merge: old_label's stats are added to new_label, old_label is then recycled.
            uint8_t ol = lp.old_label, nl = lp.new_label;
            if (ol > 0 && nl > 0 && ol <= m_max_labels && nl <= m_max_labels) {
                m_count[nl]     += m_count[ol];
                m_row_start[nl]  = std::min(m_row_start[nl], m_row_start[ol]);
                m_col_start[nl]  = std::min(m_col_start[nl], m_col_start[ol]);
                m_row_end[nl]    = std::max(m_row_end[nl],   m_row_end[ol]);
                m_col_end[nl]    = std::max(m_col_end[nl],   m_col_end[ol]);
                m_active[nl]     = true;
                // Reset old label (its blob is now absorbed; do not finalize it separately)
                m_count[ol]      = 0;
                m_row_start[ol]  = std::numeric_limits<int>::max();
                m_col_start[ol]  = std::numeric_limits<int>::max();
                m_row_end[ol]    = 0;
                m_col_end[ol]    = 0;
                m_active[ol]     = false;
                if (m_on_recycle) m_on_recycle(ol);   // old label can be reused immediately
            }
            continue;   // no further processing for merge event
        }

        // Normal pixel (non‑merge)
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

    // End of stream: finalise all labels that are still active.
    for (int i = 1; i <= m_max_labels; ++i)
        finalize_label(static_cast<uint8_t>(i));
}