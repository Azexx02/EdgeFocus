#include "behavior/PhoneFactBuffer.h"

#include <utility>

namespace edgefocus {

void PhoneFactBuffer::beginSession(std::string session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id_ = std::move(session_id);
    latest_.reset();
}

void PhoneFactBuffer::endSession() {
    std::lock_guard<std::mutex> lock(mutex_);
    session_id_.clear();
    latest_.reset();
}

bool PhoneFactBuffer::publish(const session::PhoneFact& fact, std::string* error) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (session_id_.empty()) {
        if (error) *error = "no active session";
        return false;
    }
    if (fact.session_id != session_id_) {
        if (error) *error = "phone fact belongs to another session";
        return false;
    }
    if (fact.fresh_for_ms <= 0) {
        if (error) *error = "fresh_for_ms must be positive";
        return false;
    }
    if (fact.active && !fact.category.has_value()) {
        if (error) *error = "active phone fact requires category";
        return false;
    }
    if (!fact.active && fact.category.has_value()) {
        if (error) *error = "inactive phone fact must not include category";
        return false;
    }
    if (latest_ && fact.sequence == latest_->sequence) {
        const bool same = fact.session_id == latest_->session_id &&
                          fact.source_timestamp_ms == latest_->source_timestamp_ms &&
                          fact.active == latest_->active &&
                          fact.category == latest_->category &&
                          fact.fresh_for_ms == latest_->fresh_for_ms;
        if (!same && error) *error = "phone sequence conflicts with different content";
        return same;
    }
    if (latest_ && fact.sequence < latest_->sequence) {
        if (error) *error = "out-of-order phone sequence";
        return false;
    }
    if (latest_ && fact.received_monotonic_ms < latest_->received_monotonic_ms) {
        if (error) *error = "phone receive timestamp moved backwards";
        return false;
    }
    latest_ = fact;
    return true;
}

std::optional<distraction::PhoneCategory> PhoneFactBuffer::mapCategory(
    const std::optional<session::PhoneCategory>& category) {
    if (!category) return std::nullopt;
    switch (*category) {
        case session::PhoneCategory::Learning:
            return distraction::PhoneCategory::Learning;
        case session::PhoneCategory::Entertainment:
            return distraction::PhoneCategory::Entertainment;
        case session::PhoneCategory::GeneralOrUnknown:
            return distraction::PhoneCategory::GeneralOrUnknown;
    }
    return std::nullopt;
}

std::optional<distraction::PhoneObservation> PhoneFactBuffer::observation(
    std::int64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!latest_) return std::nullopt;
    if (now_ms < latest_->received_monotonic_ms ||
        now_ms - latest_->received_monotonic_ms > latest_->fresh_for_ms) {
        return std::nullopt;
    }
    return distraction::PhoneObservation{
        latest_->received_monotonic_ms,
        latest_->active,
        mapCategory(latest_->category),
        latest_->fresh_for_ms};
}

session::PhoneSnapshot PhoneFactBuffer::snapshot(std::int64_t now_ms) const {
    std::lock_guard<std::mutex> lock(mutex_);
    session::PhoneSnapshot value;
    if (!latest_) return value;
    value.sequence = latest_->sequence;
    value.timestamp_ms = latest_->received_monotonic_ms;
    value.active = latest_->active;
    value.category = latest_->category;
    value.fresh = now_ms >= latest_->received_monotonic_ms &&
                  now_ms - latest_->received_monotonic_ms <= latest_->fresh_for_ms;
    return value;
}

}  // namespace edgefocus
