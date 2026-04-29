#pragma once
#include "IDataSource.h"
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <chrono>

/**
 * Continuous webcam source – keeps camera open, grabs frame after frame.
 * FIX: warms up the camera by discarding first few frames until exposure settles.
 * Saves the first GOOD frame as PNG so you can see what the camera actually captured.
 * Default device = /dev/video2 (external USB webcam).
 */
class WebcamDataSource : public IDataSource
{
public:
    explicit WebcamDataSource(int columns, int rows=0,
                               int cam_index=2, std::string save_path="")
        : m_columns(columns), m_rows(rows)
        , m_cam_index(cam_index), m_save_path(std::move(save_path))
    {
        if (columns % 2 != 0)
            throw std::invalid_argument("Column count m must be even.");
        open_camera();
        warmup_and_grab_first_good_frame();   // <-- FIX: discard unstable frames
    }

    ~WebcamDataSource() { if (m_cap.isOpened()) m_cap.release(); }

    bool next(uint8_t& p1, uint8_t& p2) override
    {
        // When current frame is fully consumed, grab the next live frame
        if (m_pos + 1 >= static_cast<int>(m_flat.size()))
            if (!grab_frame()) return false;
        p1 = m_flat[m_pos++];
        p2 = m_flat[m_pos++];
        return true;
    }

    void reset() override { if (!m_cap.isOpened()) open_camera(); grab_frame(); }
    int frames_captured() const { return m_frame_count; }

private:
    void open_camera()
    {
        m_cap.open(m_cam_index, cv::CAP_V4L2);
        if (!m_cap.isOpened())
            throw std::runtime_error(
                "Cannot open /dev/video" + std::to_string(m_cam_index) +
                "\n  Check: v4l2-ctl --list-devices");
        m_cap.set(cv::CAP_PROP_FRAME_WIDTH,  640);
        m_cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        std::cout << "[Webcam] /dev/video" << m_cam_index << " opened.\n";
    }

    /** Warm up: discard 5 frames to let auto‑exposure & white balance settle */
    void warmup_and_grab_first_good_frame()
    {
        std::cout << "[Webcam] Warming up, discarding first 5 frames...\n";
        cv::Mat dummy;
        for (int i = 0; i < 5; ++i) {
            m_cap >> dummy;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        // Now grab the first usable frame
        if (!grab_frame()) {
            throw std::runtime_error("Failed to grab first good frame after warmup");
        }
    }

    bool grab_frame()
    {
        cv::Mat frame;
        m_cap >> frame;
        if (frame.empty()) { std::cerr << "[Webcam] Empty frame.\n"; return false; }

        cv::Mat gray;
        cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

        int target_rows = (m_rows > 0) ? m_rows
            : std::max(1, static_cast<int>(std::round(
                  gray.rows * (static_cast<double>(m_columns) / gray.cols))));

        cv::Mat resized;
        cv::resize(gray, resized, cv::Size(m_columns, target_rows),
                   0, 0, cv::INTER_AREA);
        m_actual_rows = resized.rows;

        m_flat.clear();
        m_flat.reserve(static_cast<size_t>(m_actual_rows * m_columns));
        for (int r = 0; r < resized.rows; ++r)
            for (int c = 0; c < resized.cols; ++c)
                m_flat.push_back(resized.at<uint8_t>(r, c));
        m_pos = 0;
        ++m_frame_count;

        if (m_frame_count == 1 || m_frame_count % 30 == 0)
            std::cout << "[Webcam] Frame " << m_frame_count
                      << " — " << m_actual_rows << "x" << m_columns
                      << " = " << m_flat.size() << " pixels\n";

        // Save the FIRST good frame (after warm‑up) so you can see what camera captured
        if (!m_save_path.empty() && m_frame_count == 1)
        {
            cv::imwrite(m_save_path, resized);
            std::cout << "[Webcam] Frame saved -> " << m_save_path << "\n";
        }
        return true;
    }

    int m_columns, m_rows, m_cam_index, m_actual_rows{0};
    std::string m_save_path;
    std::vector<uint8_t> m_flat;
    int m_pos{0}, m_frame_count{0};
    cv::VideoCapture m_cap;
};