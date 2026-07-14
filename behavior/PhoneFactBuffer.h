#pragma once

#include "session/SessionDomain.h"
#include "distraction/fusion.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace edgefocus {

class PhoneFactBuffer {
public:
    void beginSession(std::string session_id);
    void endSession();

    bool publish(const session::PhoneFact& fact, std::string* error = nullptr);
    std::optional<distraction::PhoneObservation> observation(std::int64_t now_ms) const;
    session::PhoneSnapshot snapshot(std::int64_t now_ms) const;

private:
    static std::optional<distraction::PhoneCategory> mapCategory(
        const std::optional<session::PhoneCategory>& category);

    mutable std::mutex mutex_;
    std::string session_id_;
    std::optional<session::PhoneFact> latest_;
};

}  // namespace edgefocus
