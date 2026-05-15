// ============================================================================
// IDataSource.h
// Strategy interface for data sources.
// Allows swapping CSV, RNG, or webcam without changing Block 1 code.
// ============================================================================

#pragma once

#include <cstdint>   // uint8_t

/**
 * @brief Abstract interface for any source that produces pairs of 8‑bit pixels.
 *
 * Classes that implement this interface (CsvDataSource, RngDataSource, etc.)
 * must provide next() and reset().
 */
class IDataSource
{
public:
    virtual ~IDataSource() = default;   // Virtual destructor for proper cleanup.

    /**
     * @brief Retrieve the next two pixel values.
     * @param p1 Reference to store first pixel.
     * @param p2 Reference to store second pixel.
     * @return true  if two values were successfully written,
     *         false if no more data is available (end of source).
     */
    virtual bool next(uint8_t& p1, uint8_t& p2) = 0;

    /**
     * @brief Reset the source to its initial state.
     * After reset(), subsequent calls to next() will return the first values again.
     */
    virtual void reset() = 0;
};