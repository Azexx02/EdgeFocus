#include "network/AppEventOutbox.h"
#include "policy/NotificationRouter.h"

#include <algorithm>
#include <array>

namespace edgefocus {

namespace {

constexpr std::size_t kRegistryCapacity = 64;
constexpr std::size_t kOutboxCapacity = 32;
constexpr std::int64_t kAckTtlMs = 120000;
constexpr std::array<std::int64_t, 4> kRetryAtMs{{0, 500, 1500, 3000}};

}  // namespace

bool AppEventOutbox::isCritical(const std::string& command) {
    return command == "push_alert" || command == "push_session_end" ||
           command == "push_gimbal_command_result";
}

std::int64_t AppEventOutbox::deliveryTtl(const std::string& command) {
    if (command == "push_session_end") return 60000;
    if (command == "push_gimbal_command_result") return 30000;
    return 10000;
}

int AppEventOutbox::stageValue(const std::string& stage) {
    if (stage == "received") return 1;
    if (stage == "presented") return 2;
    if (stage == "user_ack") return 3;
    return 0;
}

std::string AppEventOutbox::stageName(int value) {
    if (value >= 3) return "user_ack";
    if (value == 2) return "presented";
    if (value == 1) return "received";
    return "none";
}

bool AppEventOutbox::reliableLocked() const {
    return subscribed_ && ack_capability_ && dedupe_capability_;
}

AppEventOutbox::EnqueueResult AppEventOutbox::enqueue(
    const std::string& payload,
    const std::string& fallback_session_id,
    std::int64_t now_ms) {
    auto parsed = nlohmann::json::parse(payload, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return {false, false, payload, {}};
    const std::string command = parsed.value("cmd", "");
    if (!isCritical(command)) return {false, subscribed_, payload, {}};

    std::lock_guard<std::mutex> lock(mutex_);
    auto& data = parsed["data"];
    if (!data.is_object()) data = nlohmann::json::object();
    std::string event_id = data.value("event_id", "");
    if (event_id.empty()) {
        event_id = "evt_" + std::to_string(now_ms) + "_" + std::to_string(++sequence_);
    }
    const std::string session_id = data.value("session_id", fallback_session_id);
    data["schema_version"] = "2.1";
    data["event_id"] = event_id;
    data["session_id"] = session_id;
    data["created_at_ms"] = now_ms;
    data["ttl_ms"] = deliveryTtl(command);
    data["remaining_ttl_ms"] = deliveryTtl(command);

    EventRecord event;
    event.event_id = event_id;
    event.session_id = session_id;
    event.command = command;
    event.payload = parsed.dump();
    event.level = data.value("level", 0);
    if (data.contains("phone_actions") && data["phone_actions"].is_array()) {
        for (const auto& action : data["phone_actions"]) {
            if (!action.is_string()) continue;
            const auto name = action.get<std::string>();
            if (name == "SCREEN" || name == "LIGHT") {
                event.phone_channels_mask |= kNotificationChannelScreenMask;
            } else if (name == "BAND") {
                event.phone_channels_mask |= kNotificationChannelBandMask;
            } else if (name == "SOUND") {
                event.phone_channels_mask |= kNotificationChannelSoundMask;
            }
        }
    }
    event.created_ms = now_ms;
    event.delivery_deadline_ms = now_ms + deliveryTtl(command);
    event.ack_deadline_ms = now_ms + kAckTtlMs;
    event.next_attempt_ms = now_ms;
    event.pending = true;
    events_.push_back(event);
    trimLocked();

    const bool reliable = reliableLocked();
    if (subscribed_ && !reliable) {
        events_.back().pending = false;
        events_.back().state = "sent_legacy";
        return {true, true, event.payload, event_id};
    }
    return {true, false, event.payload, event_id};
}

std::string AppEventOutbox::renderPayload(const EventRecord& event, std::int64_t now_ms) {
    auto parsed = nlohmann::json::parse(event.payload, nullptr, false);
    if (parsed.is_discarded()) return event.payload;
    parsed["data"]["remaining_ttl_ms"] =
        std::max<std::int64_t>(0, event.delivery_deadline_ms - now_ms);
    return parsed.dump();
}

std::vector<std::string> AppEventOutbox::due(std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> payloads;
    if (!reliableLocked()) return payloads;
    for (auto& event : events_) {
        if (!event.pending) continue;
        if (now_ms >= event.delivery_deadline_ms) {
            event.pending = false;
            event.state = "expired";
            continue;
        }
        const bool replay = replay_pending_;
        if (!replay && now_ms < event.next_attempt_ms) continue;
        payloads.push_back(renderPayload(event, now_ms));
        event.state = "sent";
        if (event.attempt_index < static_cast<int>(kRetryAtMs.size()) - 1) {
            ++event.attempt_index;
            event.next_attempt_ms = event.created_ms + kRetryAtMs[event.attempt_index];
        } else {
            event.next_attempt_ms = event.delivery_deadline_ms + 1;
        }
    }
    replay_pending_ = false;
    return payloads;
}

void AppEventOutbox::setSubscribed(bool subscribed) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribed_ = subscribed;
    if (!subscribed) {
        ack_capability_ = false;
        dedupe_capability_ = false;
        replay_pending_ = false;
    } else if (reliableLocked()) {
        replay_pending_ = true;
    }
}

bool AppEventOutbox::negotiate(const std::vector<std::string>& capabilities) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool was_reliable = reliableLocked();
    ack_capability_ = std::find(capabilities.begin(), capabilities.end(), "event_ack_v1") !=
                      capabilities.end();
    dedupe_capability_ = std::find(capabilities.begin(), capabilities.end(), "event_dedupe_v1") !=
                         capabilities.end();
    const bool reliable = reliableLocked();
    if (!was_reliable && reliable) replay_pending_ = true;
    return reliable;
}

AppEventOutbox::AckResult AppEventOutbox::acknowledge(
    const std::string& event_id,
    const std::string& session_id,
    const std::string& stage,
    std::int64_t now_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int requested = stageValue(stage);
    if (requested == 0) return {"unknown_stage", event_id, stage, 0};
    auto found = std::find_if(events_.begin(), events_.end(), [&](const EventRecord& event) {
        return event.event_id == event_id;
    });
    if (found == events_.end()) return {"unknown_event", event_id, stage, 0};
    if (found->session_id != session_id) return {"session_mismatch", event_id, stage, 0};
    if (now_ms > found->ack_deadline_ms) return {"expired", event_id, stage, 0};
    if (requested <= found->ack_stage) {
        return {"duplicate", event_id, stageName(found->ack_stage), found->phone_channels_mask};
    }
    found->ack_stage = requested;
    found->pending = false;
    found->state = stage;
    return {"accepted", event_id, stage, found->phone_channels_mask};
}

void AppEventOutbox::trimLocked() {
    while (events_.size() > kRegistryCapacity) events_.pop_front();
    auto pendingCount = [&] {
        return std::count_if(events_.begin(), events_.end(), [](const EventRecord& event) {
            return event.pending;
        });
    };
    while (pendingCount() > static_cast<std::ptrdiff_t>(kOutboxCapacity)) {
        auto candidate = events_.end();
        for (auto it = events_.begin(); it != events_.end(); ++it) {
            if (!it->pending || it->command == "push_session_end") continue;
            if (candidate == events_.end() || it->level < candidate->level) candidate = it;
        }
        if (candidate == events_.end()) candidate = std::find_if(
            events_.begin(), events_.end(), [](const EventRecord& event) { return event.pending; });
        if (candidate == events_.end()) break;
        candidate->pending = false;
        candidate->state = "dropped_overflow";
    }
}

nlohmann::json AppEventOutbox::snapshot(std::int64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json events = nlohmann::json::array();
    for (const auto& event : events_) {
        if (now_ms > event.ack_deadline_ms) continue;
        events.push_back({{"event_id", event.event_id},
                          {"session_id", event.session_id},
                          {"command", event.command},
                          {"level", event.level},
                          {"state", event.state},
                          {"ack_stage", stageName(event.ack_stage)},
                          {"pending", event.pending}});
    }
    return {{"subscribed", subscribed_},
            {"reliable", reliableLocked()},
            {"events", events}};
}

bool AppEventOutbox::reliablePushEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return reliableLocked();
}

}  // namespace edgefocus
