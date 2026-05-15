// ============================================================================
// WebcamDataSource.h
// Live webcam data source – implements IDataSource.
// Opens a V4L2 camera (e.g., /dev/video2), captures frames, converts to grayscale,
// resizes to the required number of columns (m) and optional rows.
// Warms up by discarding first few frames to let auto‑exposure settle.
// Saves the first good frame as a PNG for visual verification.
// ============================================================================

#pragma once

#include "IDataSource.h"                // Base interface: next(), reset()
#include <opencv2/opencv.hpp>           // OpenCV for camera capture and image processing
#include <string>                       // std::string
#include <vector>                       // std::vector for pixel buffer
#include <stdexcept>                    // std::invalid_argument, std::runtime_error
#include <iostream>                     // std::cout, std::cerr
#include <thread>                       // std::this_thread::sleep_for
#include <chrono>                       // std::chrono::milliseconds

/**
 * @brief Live camera source.
 *
 * Continuously captures frames, resizes each frame to (columns × rows),
 * flattens to a 1D vector of uint8, and then serves pixel pairs one by one.
 * When the current frame is exhausted, it grabs the next frame.
 * Includes a warm‑up phase: discards first 5 frames to avoid unstable exposure.
 */
class WebcamDataSource : public IDataSource
{
public:
    /**
     * @brief Construct a webcam source.
     * @param columns    Desired number of columns (m). Must be even.
     * @param rows       Desired number of rows (0 = auto from aspect ratio).
     * @param cam_index  V4L2 device index (e.g., 2 for /dev/video2).
     * @param save_path  File path to save the first captured frame (PNG). Empty = don't save.
     *
     * @throws std::invalid_argument if columns is not even.
     * @throws std::runtime_error if camera cannot be opened or frame grab fails.
     */
    explicit WebcamDataSource(int columns, int rows=0,
                               int cam_index=2, std::string save_path="")
        : m_columns(columns), m_rows(rows)
        , m_cam_index(cam_index), m_save_path(std::move(save_path))
    {
        if (columns % 2 != 0)
            throw std::invalid_argument("Column count m must be even.");
        open_camera();                     // Open V4L2 device
        warmup_and_grab_first_good_frame(); // Discard unstable frames and capture first good one
    }

    ~WebcamDataSource() { if (m_cap.isOpened()) m_cap.release(); }

    /**
     * @brief Provide the next two pixels from the current frame.
     * If current frame is exhausted, grab a new frame.
     * @param p1 Reference to store first pixel.
     * @param p2 Reference to store second pixel.
     * @return false only if frame grabbing fails (camera disconnected), otherwise true.
     */
    bool next(uint8_t& p1, uint8_t& p2) override
    {
        // When we have consumed all pixels of the current frame, get a new frame.
        if (m_pos + 1 >= static_cast<int>(m_flat.size()))
            if (!grab_frame()) return false;
        p1 = m_flat[m_pos++];
        p2 = m_flat[m_pos++];
        return true;
    }

    /** @brief Reset: re‑open the camera and grab a fresh frame. */
    void reset() override { if (!m_cap.isOpened()) open_camera(); grab_frame(); }

    /** @brief Return the number of frames captured so far. */
    int frames_captured() const { return m_frame_count; }

private:
    /**
     * @brief Open the V4L2 camera and set reasonable frame dimensions.
     * @throws std::runtime_error if camera cannot be opened.
     */
    void open_camera()
    {
        m_cap.open(m_cam_index, cv::CAP_V4L2);   // Open using V4L2 backend
        if (!m_cap.isOpened())
            throw std::runtime_error(
                "Cannot open /dev/video" + std::to_string(m_cam_index) +
                "\n  Check: v4l2-ctl --list-devices");
        // Request 640x480 – the camera may give a different size, but we'll resize later.
        m_cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
        m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        std::cout << "[Webcam] /dev/video" << m_cam_index << " opened.\n";
    }

    /**
     * @brief Warm up the camera by discarding initial frames.
     * Auto‑exposure and white balance need a few frames to stabilise.
     * Then grabs the first good frame (which is saved if save_path provided).
     */
    void warmup_and_grab_first_good_frame()
    {
        std::cout << "[Webcam] Warming up, discarding first 5 frames...\n";
        cv::Mat dummy;
        for (int i = 0; i < 5; ++i) {
            m_cap >> dummy;   // Read and discard
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // Now grab the first usable frame
        if (!grab_frame()) {
            throw std::runtime_error("Failed to grab first good frame after warmup");
        }
    }

    /**
     * @brief Capture a new frame from the camera, convert to grayscale,
     *        resize to (m_columns × target_rows), and flatten into m_flat.
     * @return true if frame was successfully captured and processed, false otherwise.
     */
    bool grab_frame()
    {
        cv::Mat frame;
        m_cap >> frame;          // Capture a colour frame (BGR)
        if (frame.empty()) { std::cerr << "[Webcam] Empty frame.\n"; return false; }

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);   // Convert to single channel

        // Determine number of rows: if user specified m_rows > 0, use that.
        // Otherwise, preserve aspect ratio: target_rows = round( gray.rows * (m_columns / gray.cols) )
        int target_rows = (m_rows > 0) ? m_rows
            : std::max(1, static_cast<int>(std::round(
                  gray.rows * (static_cast<double>(m_columns) / gray.cols))));

        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(m_columns, target_rows),
                   0, 0, cv::INTER_AREA);   // Downscale using area interpolation (good for shrinking)
        m_actual_rows = resized.rows;        // Store actual rows used

        // Flatten the 2D image into a 1D vector (row‑major order)
        m_flat.clear();
        m_flat.reserve(static_cast<size_t>(m_actual_rows * m_columns));
        for (int r = 0; r < resized.rows; ++r)
            for (int c = 0; c < resized.cols; ++c)
                m_flat.push_back(resized.at<uint8_t>(r, c));
        m_pos = 0;          // Reset read pointer for the new frame
        ++m_frame_count;

        // Log periodically
        if (m_frame_count == 1 || m_frame_count % 30 == 0)
            std::cout << "[Webcam] Frame " << m_frame_count
                      << " — " << m_actual_rows << "x" << m_columns
                      << " = " << m_flat.size() << " pixels\n";

        // Save the first good frame as PNG (so user can verify input)
        if (!m_save_path.empty() && m_frame_count == 1)
        {
            cv::imwrite(m_save_path, resized);
            std::cout << "[Webcam] Frame saved -> " << m_save_path << "\n";
        }
        return true;
    }

    int m_columns, m_rows, m_cam_index, m_actual_rows{0};
    std::string m_save_path;
    std::vector<uint8_t> m_flat;    // Flattened pixel buffer for current frame
    int m_pos{0};                   // Current index in m_flat
    int m_frame_count{0};           // Number of frames captured so far
    cv::VideoCapture m_cap;         // OpenCV video capture object
};