// ============================================================================
// RngDataSource.h
// Random Number Generator data source – used in production mode (no test mode).
// Implements IDataSource. Produces infinite stream of random uint8 pairs.
// Uses std::mt19937 (Mersenne Twister) for high‑quality uniform distribution.
// ============================================================================

#pragma once

#include "IDataSource.h"   // Base interface: next(), reset()
#include <random>          // std::mt19937, std::random_device, std::uniform_int_distribution
#include <cstdint>         // uint8_t

/**
 * @brief Generates uniformly distributed random 8‑bit pixel values.
 *
 * Simulates two independent ADC channels of a line‑scan camera.
 * Never ends (next() always returns true).
 */
class RngDataSource : public IDataSource
{
public:
    /**
     * @brief Construct the RNG source.
     * Seeds the Mersenne Twister with a true random value from std::random_device.
     */
    RngDataSource()
        : m_rng(std::random_device{}())   // Seed with hardware entropy
        , m_dist(0, 255)                  // Distribution over [0, 255]
    {}

    /**
     * @brief Generate two random pixel values.
     * @param p1 Reference to store first random value.
     * @param p2 Reference to store second random value.
     * @return Always true (RNG never exhausts).
     */
    bool next(uint8_t& p1, uint8_t& p2) override
    {
        p1 = static_cast<uint8_t>(m_dist(m_rng));  // Draw random number, cast to uint8_t
        p2 = static_cast<uint8_t>(m_dist(m_rng));
        return true;
    }

    /**
     * @brief Reset the generator with a new random seed.
     * This makes the sequence different from previous runs.
     */
    void reset() override { m_rng.seed(std::random_device{}()); }

private:
    std::mt19937                       m_rng;   // Mersenne Twister engine
    std::uniform_int_distribution<int> m_dist;  // Maps engine output to [0,255]
};