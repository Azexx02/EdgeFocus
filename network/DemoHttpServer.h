#pragma once

#include "app/Module.h"
#include "app/SystemStateStore.h"
#include "device/DeviceManager.h"
#include "device/GimbalMonitor.h"
#include "storage/SQLiteStore.h"

#include <atomic>
#include <cctype>
#include <exception>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

namespace edgefocus {

class PhoneFactBuffer;
class PreviewFrameStore;
namespace session {
class SessionOrchestrator;
class SessionSnapshotPublisher;
}

namespace demo_http {

enum class DemoHttpParseStatus {
    INCOMPLETE,
    COMPLETE,
    ERROR
};

struct DemoHttpParsedRequest {
    std::string method;
    std::string path;
    std::string body;
};

inline std::string trimAscii(std::string_view text) {
    size_t begin = 0;
    while (begin < text.size() &&
           std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    size_t end = text.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

inline std::string lowerAscii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

inline DemoHttpParseStatus parseDemoHttpRequestBuffer(const std::string& raw,
                                                      DemoHttpParsedRequest& request,
                                                      size_t& expected_bytes,
                                                      std::string& error) {
    constexpr size_t kMaxHeaderBytes = 16 * 1024;
    constexpr size_t kMaxBodyBytes = 64 * 1024;

    expected_bytes = 0;
    error.clear();

    const size_t header_end = raw.find("\r\n\r\n");
    if ((header_end == std::string::npos && raw.size() > kMaxHeaderBytes) ||
        (header_end != std::string::npos && header_end > kMaxHeaderBytes)) {
        error = "request header too large";
        return DemoHttpParseStatus::ERROR;
    }
    if (header_end == std::string::npos) {
        return DemoHttpParseStatus::INCOMPLETE;
    }

    const size_t body_start = header_end + 4;
    std::istringstream header_stream(raw.substr(0, header_end));
    std::string request_line;
    if (!std::getline(header_stream, request_line)) {
        error = "invalid request line";
        return DemoHttpParseStatus::ERROR;
    }
    if (!request_line.empty() && request_line.back() == '\r') {
        request_line.pop_back();
    }

    request = DemoHttpParsedRequest{};
    std::istringstream first_line(request_line);
    if (!(first_line >> request.method >> request.path)) {
        error = "invalid request line";
        return DemoHttpParseStatus::ERROR;
    }

    size_t content_length = 0;
    bool has_content_length = false;
    std::string line;
    while (std::getline(header_stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos) {
            continue;
        }
        const std::string name = lowerAscii(trimAscii(line.substr(0, colon)));
        if (name != "content-length") {
            continue;
        }
        const std::string value = trimAscii(line.substr(colon + 1));
        try {
            content_length = static_cast<size_t>(std::stoull(value));
        } catch (const std::exception&) {
            error = "invalid content-length";
            return DemoHttpParseStatus::ERROR;
        }
        if (content_length > kMaxBodyBytes) {
            error = "request body too large";
            return DemoHttpParseStatus::ERROR;
        }
        has_content_length = true;
        break;
    }

    const size_t body_bytes = has_content_length ? content_length : (raw.size() - body_start);
    if (body_start > std::numeric_limits<size_t>::max() - body_bytes) {
        error = "request body too large";
        return DemoHttpParseStatus::ERROR;
    }
    expected_bytes = body_start + body_bytes;

    if (raw.size() < expected_bytes) {
        return DemoHttpParseStatus::INCOMPLETE;
    }

    request.body = raw.substr(body_start, body_bytes);
    return DemoHttpParseStatus::COMPLETE;
}

} // namespace demo_http

class DemoHttpServer : public Module {
public:
    DemoHttpServer(SystemStateStore& store, SQLiteStore& sqlite_store,
                   DeviceManager& device_mgr, GimbalMonitor& gimbal_monitor,
                   session::SessionOrchestrator& sessions,
                   session::SessionSnapshotPublisher& snapshots,
                   PhoneFactBuffer& phone_facts,
                   PreviewFrameStore& preview_frames,
                   int port);

    bool init(AppContext& ctx) override;
    void runOnce(AppContext& ctx) override;
    void shutdown(AppContext& ctx) override;

private:
    using json = nlohmann::json;

    void serveLoop();
    std::string handleRequest(const std::string& method, const std::string& path,
                              const std::string& body);
    json handleCommand(const json& req);
    json snapshotJson() const;
    json forceFocusState(const json& data);
    json forcePrecheckGuidance(const json& data);
    json forcePhoneState(const json& data);
    json triggerReminder(const json& data);
    json setDemoCounts(const json& data);
    json setPowerState(const json& data);
    json gimbalNudge(const json& data);
    json runtimeSnapshotJson() const;
    json handleRuntimeSession(const json& data);
    json handleRuntimePhone(const json& data);
    std::string previewFrameResponse() const;

    SystemStateStore& store_;
    SQLiteStore& sqlite_store_;
    DeviceManager& device_mgr_;
    GimbalMonitor& gimbal_monitor_;
    session::SessionOrchestrator& sessions_;
    session::SessionSnapshotPublisher& snapshots_;
    PhoneFactBuffer& phone_facts_;
    PreviewFrameStore& preview_frames_;
    std::atomic<std::uint64_t> phone_sequence_{0};
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread server_thread_;
};

} // namespace edgefocus
