/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace platform {

// Result of one capture + recognize round trip.
enum class OcrErrorCode {
    None = 0,
    CameraFailed,
    UploadFailed,
    EmptyResult,
    Busy,
};

struct OcrJobResult {
    OcrErrorCode error{OcrErrorCode::None};
    std::string text;
    std::string detail;
};

// Runs entirely on a worker thread so the LVGL loop never blocks.
// A single job at a time; extra requests while busy are ignored.
class OcrService {
public:
    OcrService();
    ~OcrService();

    OcrService(const OcrService&) = delete;
    OcrService& operator=(const OcrService&) = delete;

    // Kicks off capture -> upload -> parse. Returns false if a job is running.
    bool start();

    // True while a job is in progress.
    bool busy() const { return busy_.load(); }

    // Non-blocking: picks up a finished job, if any.
    bool consume(OcrJobResult& out);

    // Endpoint of the relay, e.g. http://1.2.3.4:8000/ocr .
    void set_endpoint(std::string endpoint);
    const std::string& endpoint() const { return endpoint_; }

private:
    void run();
    OcrJobResult do_job();

    static bool run_command_capture(const std::string& photo_path, std::string& detail);
    static bool run_command_upload(const std::string& photo_path,
                                   const std::string& endpoint,
                                   std::string& response,
                                   std::string& detail);
    static bool parse_response(const std::string& response, std::string& text);

    std::string endpoint_;
    std::atomic<bool> busy_{false};
    std::atomic<bool> stop_{false};
    std::mutex result_mutex_;
    bool has_result_{false};
    OcrJobResult pending_result_;
    std::thread worker_;
};

} // namespace platform
