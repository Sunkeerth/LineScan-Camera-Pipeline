
// RngDataSource.h
#pragma once
#include "IDataSource.h"
#include <random>
#include <cstdint>

/**
 * @brief RNG data source — default / production mode.
 * Uses Mersenne Twister (std::mt19937) to generate uniform uint8 values,
 * simulating two parallel ADC channels of a real line-scan camera.
 */
class RngDataSource : public IDataSource
{
public:
    RngDataSource()
        : m_rng(std::random_device{}())
        , m_dist(0, 255)
    {}

    bool next(uint8_t& p1, uint8_t& p2) override
    {
        p1 = static_cast<uint8_t>(m_dist(m_rng));
        p2 = static_cast<uint8_t>(m_dist(m_rng));
        return true;   // RNG never exhausts
    }

    void reset() override { m_rng.seed(std::random_device{}()); }

private:
    std::mt19937                       m_rng;
    std::uniform_int_distribution<int> m_dist;
};
