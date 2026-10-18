#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace edgefocus {

class AppEventOutbox {
public:
    struct EnqueueResult {
        bool critical{};
        bool send_once{};
        std::string payload;
        std::string event_id;
    };

    struct AckResult {
        std::string status;
        std::string event_id;
        std::string stage;
        int delivered_channels_mask{};
    };

    EnqueueResult enqueue(const std::string& payload,
                          const std::string& fallback_session_id,
                          std::int64_t now_ms);
    std::vector<std::string> due(std::int64_t now_ms);

    void setSubscribed(bool subscribed);
    bool negotiate(const std::vector<std::string>& capabilities);
    AckResult acknowledge(const std::string& event_id,
                          const std::string& session_id,
                          const std::string& stage,
                          std::int64_t now_ms);

    nlohmann::json snapshot(std::int64_t now_ms) const;
    bool reliablePushEnabled() const;

private:
    struct EventRecord {
        std::string event_id;
        std::string session_id;
        std::string command;
        std::string payload;
        int level{};
        int phone_channels_mask{};
        std::int64_t created_ms{};
        std::int64_t delivery_deadline_ms{};
        std::int64_t ack_deadline_ms{};
        int attempt_index{};
        std::int64_t next_attempt_ms{};
        int ack_stage{};
        bool pending{};
        std::string state{"pending"};
    };

    static int stageValue(const std::string& stage);
    static std::string stageName(int value);
    static bool isCritical(const std::string& command);
    static std::int64_t deliveryTtl(const std::string& command);
    static std::string renderPayload(const EventRecord& event, std::int64_t now_ms);
    void trimLocked();
    bool reliableLocked() const;

    mutable std::mutex mutex_;
    std::deque<EventRecord> events_;
    std::uint64_t sequence_{};
    bool subscribed_{};
    bool ack_capability_{};
    bool dedupe_capability_{};
    bool replay_pending_{};
};

}  // namespace edgefocus
