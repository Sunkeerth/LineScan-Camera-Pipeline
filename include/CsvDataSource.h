// ============================================================================
// CsvDataSource.h
// CSV file-backed data source (test mode).
// Implements the IDataSource interface.
// Loads the whole CSV into memory at startup – no I/O during live execution.
// ============================================================================

#pragma once

#include "IDataSource.h"   // Base interface: next(), reset()
#include <string>          // std::string
#include <vector>          // std::vector
#include <stdexcept>       // std::invalid_argument, std::runtime_error
#include <fstream>         // std::ifstream
#include <sstream>         // std::istringstream

/**
 * @brief Reads pixel pairs from a CSV file.
 *
 * The CSV file must contain exactly 'columns' integers per row (comma‑separated).
 * 'columns' must be even because each call to next() reads two values.
 * All values must be in the range 0‑255 (uint8_t).
 */
class CsvDataSource : public IDataSource
{
public:
    /**
     * @brief Construct a CSV data source.
     * @param filepath Path to the CSV file.
     * @param columns  Number of columns per row. Must be even.
     *
     * @throws std::invalid_argument if columns is not even.
     * @throws std::runtime_error    if file cannot be opened or contains invalid data.
     */
    explicit CsvDataSource(const std::string& filepath, int columns)
        : m_filepath(filepath), m_columns(columns)
    {
        if (columns % 2 != 0)
            throw std::invalid_argument("Column count m must be even.");
        load();   // Read and parse the entire file into m_flat.
    }

    /**
     * @brief Get the next two pixel values from the loaded data.
     * @param p1 Reference to store first pixel (0‑255).
     * @param p2 Reference to store second pixel (0‑255).
     * @return true  if two values were successfully retrieved,
     *         false if the end of data has been reached.
     */
    bool next(uint8_t& p1, uint8_t& p2) override
    {
        if (m_pos + 1 >= static_cast<int>(m_flat.size())) return false;
        p1 = m_flat[m_pos++];
        p2 = m_flat[m_pos++];
        return true;
    }

    /** @brief Reset the read position to the beginning of the data. */
    void reset() override { m_pos = 0; }

private:
    /**
     * @brief Load and validate the CSV file.
     * Reads the file line by line, splits by commas, converts to uint8_t,
     * and appends to m_flat. Stops when 'columns' values per row are read.
     *
     * @throws std::runtime_error if file cannot be opened or a value is out of range.
     */
    void load()
    {
        std::ifstream file(m_filepath);
        if (!file.is_open())
            throw std::runtime_error("Cannot open CSV: " + m_filepath);

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty()) continue;                // Skip empty lines.
            std::istringstream ss(line);
            std::string token;
            int col = 0;
            // Read up to 'm_columns' tokens per line.
            while (std::getline(ss, token, ','))
            {
                if (col >= m_columns) break;           // Ignore extra columns.
                int val = std::stoi(token);            // Convert to integer.
                if (val < 0 || val > 255)
                    throw std::runtime_error("CSV value out of range: " + token);
                m_flat.push_back(static_cast<uint8_t>(val));
                ++col;
            }
        }
        m_pos = 0;    // Start reading from the beginning.
    }

    std::string          m_filepath;   // Path to the CSV file.
    int                  m_columns;    // Number of columns per row (must be even).
    std::vector<uint8_t> m_flat;       // Flattened pixel data (all rows concatenated).
    int                  m_pos{0};     // Current index in m_flat.
};