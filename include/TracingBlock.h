// ============================================================================
// TracingBlock.h
// Block 4 – Tracing / Statistics Gathering.
// Receives labelled pixels from LabellingBlock (via LabelledPixel structure).
// For each active label, tracks: pixel count, bounding box (min row, max row,
// min col, max col). When a label can never receive more pixels (a full row of
// zeros passes without any pixel of that label), it finalizes the blob and
// outputs a ComponentResult. Then recycles the label number back to Block 3.
// ============================================================================

#pragma once

#include "BoundedQueue.h"      // Input queue from LabellingBlock
#include "LabellingBlock.h"    // Provides LabelledPixel structure
#include <memory>              // std::shared_ptr
#include <thread>              // std::thread
#include <atomic>              // std::atomic
#include <functional>          // std::function for callbacks
#include <vector>              // std::vector for per‑label arrays
#include <cstdint>             // uint8_t, uint32_t

/**
 * @brief Final output record for a completed connected component (blob).
 *
 * Contains label number, total pixel count, and the axis‑aligned bounding box
 * (top‑left and bottom‑right corners) in row/column coordinates.
 */
struct ComponentResult {
    uint8_t  label{0};         // Component label number (1..m/2)
    uint32_t pixel_count{0};   // Number of defect pixels in this blob
    int      row_start{0}, col_start{0};  // Top‑left corner (inclusive)
    int      row_end{0},   col_end{0};    // Bottom‑right corner (inclusive)
};

class TracingBlock {
public:
    // Callback for each completed component (used by Pipeline to save results)
    using DoneCallback    = std::function<void(const ComponentResult&)>;
    // Callback to recycle a label back to LabellingBlock
    using RecycleCallback = std::function<void(uint8_t)>;

    /**
     * @brief Construct the Tracing Block.
     * @param input_queue   Queue that provides LabelledPixel from Block 3.
     * @param columns       Number of columns per row (m). Used to detect end of row.
     * @param max_labels    Maximum number of labels (m/2). Size of per‑label arrays.
     * @param on_done       Callback invoked when a blob is finalized.
     * @param on_recycle    Callback invoked to return a finished label to Block 3.
     */
    TracingBlock(std::shared_ptr<BoundedQueue<LabelledPixel>> input_queue,
                 int columns,
                 int max_labels,
                 DoneCallback    on_done,
                 RecycleCallback on_recycle);
    ~TracingBlock();

    void start();   // Launch the tracing thread.
    void stop();    // Signal thread to stop and close queue.

private:
    void run();                     // Main loop executed by the thread.
    void finalize_label(uint8_t label);   // Called when a label is complete.

    // ─── Input / configuration ──────────────────────────────────────────────
    std::shared_ptr<BoundedQueue<LabelledPixel>> m_input;  // Queue from Block 3
    int m_columns;          // Row width (m)
    int m_max_labels;       // Maximum number of labels (m/2)
    DoneCallback    m_on_done;      // Callback for finalized components
    RecycleCallback m_on_recycle;   // Callback to recycle label

    // ─── Per‑label statistics arrays (indexed by label number) ──────────────
    // These arrays have size m_max_labels+1 (since labels start from 1).
    std::vector<uint32_t> m_count;       // Number of pixels accumulated for each label
    std::vector<int>      m_row_start, m_col_start;  // Minimum row and column seen
    std::vector<int>      m_row_end,   m_col_end;    // Maximum row and column seen
    std::vector<bool>     m_active;      // Is the label currently alive (still receiving pixels)?

    // ─── Threading and row tracking ─────────────────────────────────────────
    std::thread       m_thread;          // Worker thread
    std::atomic<bool> m_running{false};  // Set to false to stop
    int               m_current_row{0};  // Current row index (increments after each full row)
    int               m_col_idx{0};      // Current column index within row (0..columns-1)
};