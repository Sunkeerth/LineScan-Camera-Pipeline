// ============================================================================
// PixelPair.h
// Envelope passed from Block 1 (DataGeneration) to Block 2 (FilterThreshold).
// Carries two 8‑bit pixel values (as read from camera/CSV/RNG) and a high‑resolution
// timestamp (steady_clock) to measure end‑to‑end latency.
// ============================================================================

#pragma once

#include <cstdint>      // uint8_t
#include <chrono>       // std::chrono::steady_clock::time_point

/**
 * @brief Two pixels emitted together by the line‑scan camera.
 *
 * The camera reads two pixels per clock cycle (e.g., from two parallel ADCs).
 * The emit_time is set right before pushing into the first queue, allowing
 * Block 2 to compute how long the pixel pair waited.
 */
struct PixelPair
{
    uint8_t  p1{0};      // First pixel value (0‑255)
    uint8_t  p2{0};      // Second pixel value (0‑255)
    std::chrono::steady_clock::time_point emit_time{};   // Timestamp of creation

    // Default constructor (required for some STL containers)
    PixelPair() = default;

    /**
     * @brief Construct a PixelPair with given values and timestamp.
     * @param a First pixel (uint8)
     * @param b Second pixel (uint8)
     * @param t Time point when this pair was generated.
     */
    PixelPair(uint8_t a, uint8_t b, std::chrono::steady_clock::time_point t)
        : p1(a), p2(b), emit_time(t) {}
};