// ============================================================================
// LabellingBlock.cpp
// Implementation of Block 3 – Connected Component Labelling.
// Reads thresholded pixels (0/1) from Block 2. Uses 8‑connectivity (left, top‑left,
// top, top‑right neighbours). Only keeps one row of history (m_prev_row) to satisfy
// memory constraints. Uses union‑find (parent + rank arrays, each of size m/2)
// to merge labels when two blobs become connected. Sends labelled pixels and
// merge events to Block 4.
// ============================================================================

#include "LabellingBlock.h"
#include <algorithm>   // std::fill, std::swap, etc.

// ----------------------------------------------------------------------------
// Constructor: initialises row buffers, union‑find arrays, and label pool.
// m_max_labels = columns/2 (max active labels at once, as per assignment).
// ----------------------------------------------------------------------------
LabellingBlock::LabellingBlock(
    std::shared_ptr<BoundedQueue<FilteredOutput>> input_queue,  // from Block 2
    int columns,                                                // row width (m)
    OutputCallback on_output)                                   // sends LabelledPixel to Block 4
    : m_input(std::move(input_queue))
    , m_columns(columns)
    , m_on_output(std::move(on_output))
    , m_prev_row(columns, 0)     // previous row labels (uint16, but stored as uint16_t)
    , m_curr_row(columns, 0)     // current row being filled
    , m_max_labels(columns / 2)  // max number of labels that can coexist
    , m_parent(columns / 2 + 1, 0)   // union‑find parent array (index 0 unused)
    , m_rank  (columns / 2 + 1, 0)   // union‑find rank array (for union by rank)
{
    // Initialise union‑find: each label is its own root (self‑parent)
    for (int i = 0; i <= m_max_labels; ++i)
        m_parent[i] = static_cast<uint8_t>(i);
}

LabellingBlock::~LabellingBlock() { stop(); }

// ----------------------------------------------------------------------------
// Union‑Find: find the root of a label with path compression.
// ----------------------------------------------------------------------------
uint8_t LabellingBlock::find_root(uint8_t label) {
    while (m_parent[label] != label) {
        m_parent[label] = m_parent[m_parent[label]]; // path compression (halves path length)
        label = m_parent[label];
    }
    return label;
}

// ----------------------------------------------------------------------------
// Union‑Find: merge two labels. Uses union by rank to keep trees shallow.
// ----------------------------------------------------------------------------
void LabellingBlock::union_labels(uint8_t a, uint8_t b) {
    uint8_t ra = find_root(a), rb = find_root(b);
    if (ra == rb) return;
    if (m_rank[ra] < m_rank[rb]) std::swap(ra, rb);
    m_parent[rb] = ra;
    if (m_rank[ra] == m_rank[rb]) ++m_rank[ra];
}

// ----------------------------------------------------------------------------
// Get a fresh label number. First tries to reuse from m_free_labels (recycled by Block 4).
// If none, allocates a new sequential label (1,2,3... up to m_max_labels).
// Returns 0 only if out of labels (should not happen with correct recycling).
// ----------------------------------------------------------------------------
uint8_t LabellingBlock::next_free_label() {
    std::lock_guard<std::mutex> lk(m_recycle_mutex);
    if (!m_free_labels.empty()) {
        uint8_t l = m_free_labels.back();
        m_free_labels.pop_back();
        m_parent[l] = l;   // reset union‑find parent to self
        m_rank[l]   = 0;
        return l;
    }
    if (m_next_label <= static_cast<uint8_t>(m_max_labels))
        return m_next_label++;
    return 0; // should never happen
}

// ----------------------------------------------------------------------------
// Called by Block 4 (via Pipeline) to return a finished label for reuse.
// ----------------------------------------------------------------------------
void LabellingBlock::recycle_label(uint8_t label) {
    std::lock_guard<std::mutex> lk(m_recycle_mutex);
    m_free_labels.push_back(label);
}

void LabellingBlock::start() {
    m_running = true;
    m_thread  = std::thread(&LabellingBlock::run, this);
}

void LabellingBlock::stop() {
    m_running = false;
    m_input->close();   // unblock the pop() in run()
    if (m_thread.joinable()) m_thread.join();
}

// ----------------------------------------------------------------------------
// Main labelling loop.
// For each incoming thresholded pixel (val = 0 or 1), if val==1:
//   - Check up to 4 already‑processed neighbours: left (curr row), top‑left, top, top‑right (prev row)
//   - Collect their root labels (non‑zero)
//   - If no neighbour -> assign new label
//   - If neighbours -> pick the first as candidate, merge all others into it
//   - Emit LabelledPixel (with optional merge event) to Block 4
// At end of each row, swap prev/curr rows, clear curr row, and reset union‑find arrays
// (because labels from previous rows are no longer needed – they are now finalised by Block 4).
// ----------------------------------------------------------------------------
void LabellingBlock::run() {
    while (m_running) {
        auto maybe = m_input->pop();
        if (!maybe.has_value()) break;

        const FilteredOutput& pixel = maybe.value();
        uint8_t val = pixel.thresholded;   // 0 or 1
        int c = m_col_idx;                 // current column index (0..m_columns-1)

        uint8_t label = 0;

        if (val == 1) {
            // Check neighbours (8‑connectivity). Only consider those already processed.
            // nbr[0] = left (current row)
            // nbr[1] = top‑left (previous row)
            // nbr[2] = top (previous row)
            // nbr[3] = top‑right (previous row)
            uint8_t nbr[4] = {0, 0, 0, 0};
            if (c > 0)            nbr[0] = find_root(static_cast<uint8_t>(m_curr_row[c-1]));
            if (c > 0)            nbr[1] = find_root(static_cast<uint8_t>(m_prev_row[c-1]));
            nbr[2] = find_root(static_cast<uint8_t>(m_prev_row[c]));
            if (c < m_columns-1)  nbr[3] = find_root(static_cast<uint8_t>(m_prev_row[c+1]));

            // Collect unique non‑zero neighbour roots
            uint8_t found[4]; int nfound = 0;
            for (int i = 0; i < 4; ++i)
                if (nbr[i] != 0) {
                    bool dup = false;
                    for (int j = 0; j < nfound; ++j)
                        if (found[j] == nbr[i]) { dup = true; break; }
                    if (!dup) found[nfound++] = nbr[i];
                }

            if (nfound == 0) {
                // No neighbour defect: start a new blob
                label = next_free_label();
            } else {
                label = found[0];
                // Merge all other distinct neighbour labels into 'label'
                for (int i = 1; i < nfound; ++i) {
                    if (find_root(found[i]) != find_root(label)) {
                        uint8_t old_l = find_root(found[i]);
                        uint8_t new_l = find_root(label);
                        union_labels(label, found[i]);
                        new_l = find_root(label);  // after union, root may have changed
                        // Notify Block 4 about this merge
                        LabelledPixel merge_ev;
                        merge_ev.merge_occurred = true;
                        merge_ev.old_label = old_l;
                        merge_ev.new_label = new_l;
                        if (m_on_output) m_on_output(merge_ev);
                    }
                }
                label = find_root(label); // ensure we have the root
            }
        }

        m_curr_row[c] = label;
        ++m_col_idx;

        // Emit normal (non‑merge) labelled pixel
        LabelledPixel out;
        out.value = val;
        out.label = label;
        out.merge_occurred = false;
        if (m_on_output) m_on_output(out);

        // End of row: rotate row buffers and reset union‑find
        if (m_col_idx >= m_columns) {
            m_col_idx = 0;
            std::swap(m_prev_row, m_curr_row);            // previous row becomes old current
            std::fill(m_curr_row.begin(), m_curr_row.end(), 0); // clear current row
            // Reset union‑find for the next row. Labels from previous rows are no longer needed
            // because any new connection can only happen within one row of history.
            for (int i = 0; i <= m_max_labels; ++i) {
                m_parent[i] = static_cast<uint8_t>(i);
                m_rank[i]   = 0;
            }
        }
    }
}