#include "network/AppEventOutbox.h"
#include "policy/NotificationRouter.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using edgefocus::AppEventOutbox;
using nlohmann::json;

namespace {

void require_always(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    std::exit(1);
}

#undef assert
#define assert(expression) require_always((expression), #expression, __LINE__)

std::string alertPayload(int level, const std::string& session_id = "session_1") {
    return json{{"cmd", "push_alert"},
                {"data", {{"session_id", session_id},
                          {"level", level},
                          {"phone_actions", json::array({"SCREEN", "SOUND"})}}}}.dump();
}

void test_capability_gated_retry_and_ack() {
    AppEventOutbox outbox;
    auto queued = outbox.enqueue(alertPayload(2), "fallback", 1000);
    assert(queued.critical);
    assert(!queued.send_once);
    assert(outbox.due(1000).empty());

    outbox.setSubscribed(true);
    assert(outbox.negotiate({"event_ack_v1", "event_dedupe_v1"}));
    auto first = outbox.due(1000);
    assert(first.size() == 1);
    auto payload = json::parse(first.front());
    const auto event_id = payload["data"]["event_id"].get<std::string>();
    assert(payload["data"]["schema_version"] == "2.1");
    assert(payload["data"]["remaining_ttl_ms"] == 10000);

    assert(outbox.due(1499).empty());
    assert(outbox.due(1500).size() == 1);

    auto ack = outbox.acknowledge(event_id, "session_1", "received", 1600);
    assert(ack.status == "accepted");
    assert((ack.delivered_channels_mask & edgefocus::kNotificationChannelScreenMask) != 0);
    assert((ack.delivered_channels_mask & edgefocus::kNotificationChannelSoundMask) != 0);
    assert(outbox.due(3000).empty());

    auto duplicate = outbox.acknowledge(event_id, "session_1", "received", 1700);
    assert(duplicate.status == "duplicate");
    auto presented = outbox.acknowledge(event_id, "session_1", "presented", 1800);
    assert(presented.status == "accepted");
    auto stale_stage = outbox.acknowledge(event_id, "session_1", "received", 1900);
    assert(stale_stage.status == "duplicate");
    assert(stale_stage.stage == "presented");
}

void test_legacy_client_gets_one_send() {
    AppEventOutbox outbox;
    outbox.setSubscribed(true);
    auto queued = outbox.enqueue(alertPayload(1), "fallback", 2000);
    assert(queued.critical);
    assert(queued.send_once);
    assert(!queued.payload.empty());
    assert(outbox.due(2000).empty());
}

void test_outbox_is_bounded_and_preserves_session_end() {
    AppEventOutbox outbox;
    outbox.setSubscribed(true);
    outbox.negotiate({"event_ack_v1", "event_dedupe_v1"});
    outbox.enqueue(json{{"cmd", "push_session_end"},
                        {"data", {{"session_id", "session_1"}}}}.dump(),
                   "session_1", 3000);
    for (int i = 0; i < 12; ++i) {
        outbox.enqueue(alertPayload((i % 3) + 1), "session_1", 3001 + i);
    }
    auto snapshot = outbox.snapshot(3015);
    int pending = 0;
    bool session_end_pending = false;
    for (const auto& event : snapshot["events"]) {
        if (event["pending"].get<bool>()) {
            ++pending;
            if (event["command"] == "push_session_end") session_end_pending = true;
        }
    }
    assert(pending <= 8);
    assert(session_end_pending);
}

}  // namespace

int main() {
    test_capability_gated_retry_and_ack();
    test_legacy_client_gets_one_send();
    test_outbox_is_bounded_and_preserves_session_end();
    std::cout << "All AppEventOutbox tests passed!\n";
    return 0;
}
