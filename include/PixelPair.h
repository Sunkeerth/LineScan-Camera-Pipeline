// PixelPair.h

#pragma once
#include <cstdint>
#include <chrono>

/**
 * @brief Envelope passed between Block 1 and Block 2 via the shared queue.
 * Carries two pixel values and a timestamp for timing verification.
 */
struct PixelPair
{
    uint8_t  p1{0};
    uint8_t  p2{0};
    std::chrono::steady_clock::time_point emit_time{};

    PixelPair() = default;
    PixelPair(uint8_t a, uint8_t b, std::chrono::steady_clock::time_point t)
        : p1(a), p2(b), emit_time(t) {}
};
