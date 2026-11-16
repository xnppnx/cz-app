/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "ocr_service.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

namespace platform {
namespace {

constexpr const char* kPhotoPath = "/tmp/snapread_capture.jpg";
constexpr const char* kResponsePath = "/tmp/snapread_response.txt";
constexpr const char* kCurlErrorPath = "/tmp/snapread_curl_err.txt";

// Wrap a value in single quotes for safe shell use (end embedded quotes).
std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out += c;
        }
    }
    out += "'";
    return out;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool write_empty(const std::string& path) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    return out.good();
}

// Extract the value of a JSON string field by key. Handles basic \\u escapes
// well enough for the relay's plain response; no external JSON dependency.
std::string json_string_field(const std::string& json, const char* key) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    const auto key_pos = json.find(needle);
    if (key_pos == std::string::npos) {
        return {};
    }
    auto colon = json.find(':', key_pos + needle.size());
    if (colon == std::string::npos) {
        return {};
    }
    auto open = json.find('"', colon + 1);
    if (open == std::string::npos) {
        return {};
    }
    std::string value;
    bool escaped = false;
    for (std::size_t i = open + 1; i < json.size(); ++i) {
        char c = json[i];
        if (escaped) {
            switch (c) {
                case 'n': value += '\n'; break;
                case 'r': value += '\r'; break;
                case 't': value += '\t'; break;
                case '"': value += '"'; break;
                case '\\': value += '\\'; break;
                case '/': value += '/'; break;
                default: value += c; break;
            }
            escaped = false;
            continue;
        }
        if (c == '\\') {
            escaped = true;
            continue;
        }
        if (c == '"') {
            break;
        }
        value += c;
    }
    return value;
}

} // namespace

OcrService::OcrService() {
    worker_ = std::thread([this] { run(); });
}

OcrService::~OcrService() {
    stop_.store(true);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void OcrService::set_endpoint(std::string endpoint) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    endpoint_ = std::move(endpoint);
}

bool OcrService::start() {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true)) {
        return false;
    }
    return true;
}

bool OcrService::consume(OcrJobResult& out) {
    std::lock_guard<std::mutex> lock(result_mutex_);
    if (!has_result_) {
        return false;
    }
    out = pending_result_;
    has_result_ = false;
    return true;
}

void OcrService::run() {
    while (!stop_.load()) {
        if (!busy_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            continue;
        }

        OcrJobResult result = do_job();
        {
            std::lock_guard<std::mutex> lock(result_mutex_);
            pending_result_ = std::move(result);
            has_result_ = true;
        }
        busy_.store(false);
    }
}

OcrJobResult OcrService::do_job() {
    OcrJobResult result;

    // 1) Capture a still image with the on-device rpicam-jpeg tool.
    std::string capture_detail;
    if (!run_command_capture(kPhotoPath, capture_detail)) {
        result.error = OcrErrorCode::CameraFailed;
        result.detail = capture_detail;
        return result;
    }

    // 2) POST the image to the relay and capture the response body.
    std::string endpoint;
    {
        std::lock_guard<std::mutex> lock(result_mutex_);
        endpoint = endpoint_;
    }
    if (endpoint.empty()) {
        result.error = OcrErrorCode::UploadFailed;
        result.detail = "No recognition server configured";
        return result;
    }

    std::string response;
    std::string upload_detail;
    if (!run_command_upload(kPhotoPath, endpoint, response, upload_detail)) {
        result.error = OcrErrorCode::UploadFailed;
        result.detail = upload_detail;
        return result;
    }

    // 3) Extract recognized text from the JSON response.
    std::string text;
    if (!parse_response(response, text) || text.empty()) {
        result.error = OcrErrorCode::EmptyResult;
        result.detail = response.size() > 120 ? response.substr(0, 120) : response;
        return result;
    }

    result.error = OcrErrorCode::None;
    result.text = std::move(text);
    return result;
}

bool OcrService::run_command_capture(const std::string& photo_path, std::string& detail) {
    std::error_code ec;
    std::filesystem::remove(photo_path, ec);

    // 1640x1232 / quality 85: sharp enough for printed text, small enough to upload fast.
    std::string command =
        "rpicam-jpeg -n -t 1200 --width 1640 --height 1232 -q 85 -o " +
        shell_quote(photo_path) + " >/dev/null 2>" + shell_quote(kCurlErrorPath);
    int status = std::system(command.c_str());
    if (status != 0 || !std::filesystem::exists(photo_path, ec)) {
        detail = read_file(kCurlErrorPath);
        if (detail.empty()) {
            detail = "camera command failed";
        }
        return false;
    }
    return true;
}

bool OcrService::run_command_upload(const std::string& photo_path,
                                    const std::string& endpoint,
                                    std::string& response,
                                    std::string& detail) {
    write_empty(kResponsePath);
    write_empty(kCurlErrorPath);

    std::string command =
        "curl -sS --max-time 60 -X POST -F " +
        shell_quote("image=@" + photo_path) + " " + shell_quote(endpoint) +
        " -o " + shell_quote(kResponsePath) + " 2>" + shell_quote(kCurlErrorPath);
    int status = std::system(command.c_str());
    response = read_file(kResponsePath);
    if (status != 0) {
        detail = read_file(kCurlErrorPath);
        if (detail.empty()) {
            detail = "upload command failed";
        }
        return false;
    }
    return true;
}

bool OcrService::parse_response(const std::string& response, std::string& text) {
    // Preferred: {"ok":true,"text":"..."}
    text = json_string_field(response, "text");
    if (!text.empty()) {
        return true;
    }
    // Fallback: {"error":"..."} -> surface the message as detail later.
    return false;
}

} // namespace platform
