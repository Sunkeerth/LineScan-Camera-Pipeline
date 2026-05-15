// ============================================================================
// LabellingBlock.h
// Block 3 – Connected Component Labelling.
// Reads thresholded pixels (0/1) from Block 2, assigns a label number to each
// defect pixel (1). Uses 8‑connectivity and union‑find to merge blobs when they
// become connected through a new pixel. Only keeps one row of history
// (m_prev_row) to satisfy memory constraints.
// ============================================================================

#pragma once

#include "BoundedQueue.h"            // Input queue (receives FilteredOutput from Block 2)
#include "FilterThresholdBlock.h"    // Provides FilteredOutput structure
#include <memory>                    // std::shared_ptr, std::unique_ptr
#include <thread>                    // std::thread
#include <atomic>                    // std::atomic
#include <functional>                // std::function (OutputCallback)
#include <vector>                    // std::vector for rows and union‑find
#include <cstdint>                   // uint8_t, uint16_t
#include <mutex>                     // std::mutex for recycling thread‑safety

/**
 * @brief Output structure sent from LabellingBlock to TracingBlock.
 *
 * Contains the labelled pixel (label != 0 for defect) and optionally a merge event.
 * When a merge occurs, old_label is replaced by new_label.
 */
struct LabelledPixel {
    uint8_t  label{0};       // 0 = background (non‑defect), >0 = component label
    uint8_t  value{0};       // original thresholded value (should be 1 for defect)
    bool     merge_occurred{false};  // True if this pixel causes a merge
    uint8_t  old_label{0};   // Label that is being absorbed (the one that disappears)
    uint8_t  new_label{0};   // Label that survives (the root after union)
};

class LabellingBlock {
public:
    /**
     * @brief Callback type for each processed pixel (sent to TracingBlock).
     * @param labelled_pixel Contains label, merge info, etc.
     */
    using OutputCallback = std::function<void(const LabelledPixel&)>;

    /**
     * @brief Construct the Labelling Block.
     * @param input_queue   Queue that provides FilteredOutput (defect flags) from Block 2.
     * @param columns       Number of columns per row (m). Determines row size and max labels (m/2).
     * @param on_output     Callback invoked for every labelled pixel (typically pushes to TracingBlock's queue).
     */
    LabellingBlock(std::shared_ptr<BoundedQueue<FilteredOutput>> input_queue,
                   int columns,
                   OutputCallback on_output = nullptr);
    ~LabellingBlock();

    void start();   // Launch the labelling thread.
    void stop();    // Signal thread to stop and close queues.

    /**
     * @brief Called by TracingBlock to return a finished label number for reuse.
     * @param label Label that is no longer active (can be reused for a new blob).
     */
    void recycle_label(uint8_t label);

private:
    void run();                         // Main loop executed by the thread.
    uint8_t next_free_label();          // Get a new label from the free pool or allocate new one.
    uint8_t find_root(uint8_t label);   // Union‑Find: find the root of a label (with path compression).
    void    union_labels(uint8_t a, uint8_t b);  // Union two labels (merge two components).

    // ─── Input / configuration ──────────────────────────────────────────────
    std::shared_ptr<BoundedQueue<FilteredOutput>> m_input;  // Queue from Block 2 (0/1 values).
    int            m_columns;      // Row width (m). Used to index neighbours.
    OutputCallback m_on_output;     // Callback to send LabelledPixel to Block 4.

    // ─── Row history (strict memory: only one previous row) ─────────────────
    // According to assignment: "only one row of history stored (prev_row) - datatype uint16"
    // We use uint16_t because label numbers can go up to m/2 (max ~ few hundred).
    std::vector<uint16_t> m_prev_row;   // Labels of the previous row (size = columns).
    std::vector<uint16_t> m_curr_row;   // Labels being built for the current row.

    // ─── Union‑Find structures (strict: two arrays of size m/2) ─────────────
    // m_max_labels = columns/2 (because max active labels at once is m/2).
    std::vector<uint8_t>  m_parent;     // Parent array: parent[label] = root or parent.
    std::vector<uint8_t>  m_rank;       // Rank array: used for union by rank (tree balancing).
    int      m_max_labels;              // Maximum number of labels that can exist simultaneously = m/2.

    // ─── Label recycling pool ───────────────────────────────────────────────
    // When a label is finished (blob completed), TracingBlock calls recycle_label().
    // That label is pushed into m_free_labels to be reused.
    std::vector<uint8_t>  m_free_labels;  // List of label numbers that are available.
    uint8_t               m_next_label{1}; // Next unused label number (if free pool empty).
    std::mutex            m_recycle_mutex; // Protects m_free_labels and m_next_label.

    // ─── Threading ──────────────────────────────────────────────────────────
    std::thread        m_thread;           // Worker thread.
    std::atomic<bool>  m_running{false};   // Set to false to stop the thread.
    int                m_col_idx{0};       // Current column index within a row (0..columns-1).
};