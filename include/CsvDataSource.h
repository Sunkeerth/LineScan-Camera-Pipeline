
// CsvDataSource.h
#pragma once
#include "IDataSource.h"
#include <string>
#include <vector>
#include <stdexcept>
#include <fstream>
#include <sstream>

/**
 * @brief CSV file-backed data source — test mode.
 *
 * Loads the entire file into memory at startup (m_flat[]).
 * next() just advances a pointer by 2 — no I/O during live execution.
 * Returns false when all values are consumed.
 */
class CsvDataSource : public IDataSource
{
public:
    explicit CsvDataSource(const std::string& filepath, int columns)
        : m_filepath(filepath), m_columns(columns)
    {
        if (columns % 2 != 0)
            throw std::invalid_argument("Column count m must be even.");
        load();
    }

    bool next(uint8_t& p1, uint8_t& p2) override
    {
        if (m_pos + 1 >= static_cast<int>(m_flat.size())) return false;
        p1 = m_flat[m_pos++];
        p2 = m_flat[m_pos++];
        return true;
    }

    void reset() override { m_pos = 0; }

private:
    void load()
    {
        std::ifstream file(m_filepath);
        if (!file.is_open())
            throw std::runtime_error("Cannot open CSV: " + m_filepath);

        std::string line;
        while (std::getline(file, line))
        {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string token;
            int col = 0;
            while (std::getline(ss, token, ','))
            {
                if (col >= m_columns) break;
                int val = std::stoi(token);
                if (val < 0 || val > 255)
                    throw std::runtime_error("CSV value out of range: " + token);
                m_flat.push_back(static_cast<uint8_t>(val));
                ++col;
            }
        }
        m_pos = 0;
    }

    std::string          m_filepath;
    int                  m_columns;
    std::vector<uint8_t> m_flat;
    int                  m_pos{0};
};
