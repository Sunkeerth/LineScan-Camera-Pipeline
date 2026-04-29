// IDataSource.h

#pragma once
#include <cstdint>

/**
 * @brief Strategy interface for data sources.
 * Allows switching between CSV, RNG, and Webcam without changing any other code.
 */
class IDataSource
{
public:
    virtual ~IDataSource() = default;
    virtual bool next(uint8_t& p1, uint8_t& p2) = 0;
    virtual void reset() = 0;
};
