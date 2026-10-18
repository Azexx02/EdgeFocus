#include "behavior/DistractionAssessmentEngine.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require_always(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    std::exit(1);
}

#undef assert
#define assert(expression) require_always((expression), #expression, __LINE__)

using namespace edgefocus;

session::PhoneFact fact(
    const std::string& session_id,
    std::uint64_t sequence,
    std::int64_t timestamp_ms,
    std::optional<session::PhoneCategory> category) {
    return {
        session_id,
        sequence,
        timestamp_ms,
        timestamp_ms,
        category.has_value(),
        category,
        1000};
}

EvidenceValue evidence(std::int64_t timestamp_ms, double value) {
    return {timestamp_ms, value, true, 500};
}

void test_phone_buffer_contract() {
    PhoneFactBuffer buffer;
    std::string error;
    assert(!buffer.publish(fact("s1", 1, 0, session::PhoneCategory::Entertainment), &error));
    assert(error == "no active session");

    buffer.beginSession("s1");
    assert(!buffer.publish(fact("old", 1, 0, session::PhoneCategory::Entertainment), &error));
    assert(buffer.publish(fact("s1", 1, 100, session::PhoneCategory::Entertainment), &error));
    auto duplicate = fact("s1", 1, 100, session::PhoneCategory::Entertainment);
    duplicate.received_monotonic_ms = 900;
    assert(buffer.publish(duplicate, &error));
    assert(!buffer.publish(fact("s1", 1, 101, session::PhoneCategory::GeneralOrUnknown), &error));
    assert(buffer.observation(1100).has_value());
    assert(!buffer.observation(1101).has_value());
    auto stale = buffer.snapshot(1101);
    assert(!stale.fresh && stale.active);

    buffer.beginSession("s2");
    assert(!buffer.snapshot(0).active);
}

void test_phone_category_continuity_through_adapter() {
    PhoneFactBuffer buffer;
    DistractionAssessmentEngine engine;
    buffer.beginSession("s1");
    engine.beginSession("s1");

    assert(buffer.publish(fact("s1", 1, 0, session::PhoneCategory::Entertainment)));
    auto assessment = engine.update(0, evidence(0, 1.0), evidence(0, 0.0), buffer.observation(0));
    assert(assessment.applicability == session::Applicability::Active);
    assert(assessment.level == 0);

    assert(buffer.publish(fact("s1", 2, 2000, session::PhoneCategory::Entertainment)));
    assessment = engine.update(
        2000, evidence(2000, 1.0), evidence(2000, 0.0), buffer.observation(2000));
    assert(assessment.level == 1);

    assert(buffer.publish(fact("s1", 3, 2500, session::PhoneCategory::GeneralOrUnknown)));
    assessment = engine.update(
        2500, evidence(2500, 1.0), evidence(2500, 0.0), buffer.observation(2500));
    assert(assessment.level == 1);

    assert(buffer.publish(fact("s1", 4, 5000, session::PhoneCategory::GeneralOrUnknown)));
    assessment = engine.update(
        5000, evidence(5000, 1.0), evidence(5000, 0.0), buffer.observation(5000));
    assert(assessment.level == 2);

    assert(buffer.publish(fact("s1", 5, 5100, session::PhoneCategory::Learning)));
    assessment = engine.update(
        5100, evidence(5100, 1.0), evidence(5100, 0.0), buffer.observation(5100));
    assert(assessment.level == 0);

    engine.endSession();
    bool threw = false;
    try {
        engine.update(5200, evidence(5200, 1.0), evidence(5200, 0.0), std::nullopt);
    } catch (...) {
        threw = true;
    }
    assert(threw);
}

}  // namespace

int main() {
    test_phone_buffer_contract();
    test_phone_category_continuity_through_adapter();
    return 0;
}
