#include "LabellingBlock.h"
#include <algorithm>

LabellingBlock::LabellingBlock(
    std::shared_ptr<BoundedQueue<FilteredOutput>> input_queue,
    int columns,
    OutputCallback on_output)
    : m_input(std::move(input_queue))
    , m_columns(columns)
    , m_on_output(std::move(on_output))
    , m_prev_row(columns, 0)
    , m_curr_row(columns, 0)
    , m_max_labels(columns / 2)
    , m_parent(columns / 2 + 1, 0)  // array 1: size m/2
    , m_rank  (columns / 2 + 1, 0)  // array 2: size m/2
{
    // Initialize union-find: each label is its own root
    for (int i = 0; i <= m_max_labels; ++i)
        m_parent[i] = static_cast<uint8_t>(i);
}

LabellingBlock::~LabellingBlock() { stop(); }

// Union-Find: find root with path compression
uint8_t LabellingBlock::find_root(uint8_t label) {
    while (m_parent[label] != label) {
        m_parent[label] = m_parent[m_parent[label]]; // path compression
        label = m_parent[label];
    }
    return label;
}

// Union-Find: merge two labels (union by rank)
void LabellingBlock::union_labels(uint8_t a, uint8_t b) {
    uint8_t ra = find_root(a), rb = find_root(b);
    if (ra == rb) return;
    if (m_rank[ra] < m_rank[rb]) std::swap(ra, rb);
    m_parent[rb] = ra;
    if (m_rank[ra] == m_rank[rb]) ++m_rank[ra];
}

uint8_t LabellingBlock::next_free_label() {
    std::lock_guard<std::mutex> lk(m_recycle_mutex);
    if (!m_free_labels.empty()) {
        uint8_t l = m_free_labels.back();
        m_free_labels.pop_back();
        m_parent[l] = l;   // reset in union-find
        m_rank[l]   = 0;
        return l;
    }
    if (m_next_label <= static_cast<uint8_t>(m_max_labels))
        return m_next_label++;
    return 0; // should not happen with proper recycling
}

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
    m_input->close();
    if (m_thread.joinable()) m_thread.join();
}

void LabellingBlock::run() {
    while (m_running) {
        auto maybe = m_input->pop();
        if (!maybe.has_value()) break;

        const FilteredOutput& pixel = maybe.value();
        uint8_t val = pixel.thresholded;
        int c = m_col_idx;

        uint8_t label = 0;

        if (val == 1) {
            // Check 8-connected neighbours (only already-processed ones)
            // Left in current row, top-left/top/top-right from prev row
            uint8_t nbr[4] = {0, 0, 0, 0};
            if (c > 0)            nbr[0] = find_root(static_cast<uint8_t>(m_curr_row[c-1]));
            if (c > 0)            nbr[1] = find_root(static_cast<uint8_t>(m_prev_row[c-1]));
            nbr[2] = find_root(static_cast<uint8_t>(m_prev_row[c]));
            if (c < m_columns-1)  nbr[3] = find_root(static_cast<uint8_t>(m_prev_row[c+1]));

            // Collect unique non-zero neighbour labels
            uint8_t found[4]; int nfound = 0;
            for (int i = 0; i < 4; ++i)
                if (nbr[i] != 0) {
                    bool dup = false;
                    for (int j = 0; j < nfound; ++j) if (found[j]==nbr[i]) { dup=true; break; }
                    if (!dup) found[nfound++] = nbr[i];
                }

            if (nfound == 0) {
                // No neighbours: brand new label
                label = next_free_label();
            } else {
                label = found[0];
                // Merge all other neighbour labels into label
                for (int i = 1; i < nfound; ++i) {
                    if (find_root(found[i]) != find_root(label)) {
                        uint8_t old_l = find_root(found[i]);
                        uint8_t new_l = find_root(label);
                        union_labels(label, found[i]);
                        new_l = find_root(label);
                        // Notify TracingBlock of the merge
                        LabelledPixel merge_ev;
                        merge_ev.merge_occurred = true;
                        merge_ev.old_label = old_l;
                        merge_ev.new_label = new_l;
                        if (m_on_output) m_on_output(merge_ev);
                    }
                }
                label = find_root(label);
            }
        }

        m_curr_row[c] = label;
        ++m_col_idx;

        // Emit labelled pixel
        LabelledPixel out;
        out.value = val;
        out.label = label;
        out.merge_occurred = false;
        if (m_on_output) m_on_output(out);

        // End of row: rotate prev/curr, reset curr
        if (m_col_idx >= m_columns) {
            m_col_idx = 0;
            std::swap(m_prev_row, m_curr_row);
            std::fill(m_curr_row.begin(), m_curr_row.end(), 0);
            // Reset union-find for new row
            for (int i = 0; i <= m_max_labels; ++i) {
                m_parent[i] = static_cast<uint8_t>(i);
                m_rank[i]   = 0;
            }
        }
    }
}
