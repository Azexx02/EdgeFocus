#include "distraction/fusion.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
distraction::TimedEvidence evidence(std::int64_t t, double value) {
    return {t, value, true, 500};
}
distraction::PhoneObservation phone(
    std::int64_t t, std::optional<distraction::PhoneCategory> category) {
    return {t, category.has_value(), category, 1000};
}
}

int main() {
    using namespace distraction;
    FusionCoreCpp fusion;
    auto a = fusion.update(0, evidence(0, 1.0), evidence(0, 0.0), phone(0, PhoneCategory::Entertainment));
    require(a.applicability == Applicability::Active && a.level == 0, "initial active level zero");
    a = fusion.update(2000, evidence(2000, 1.0), evidence(2000, 0.0), phone(2000, PhoneCategory::Entertainment));
    require(a.level == 1, "entertainment confirms at 2 seconds");
    a = fusion.update(2500, evidence(2500, 1.0), evidence(2500, 0.0), phone(2500, PhoneCategory::GeneralOrUnknown));
    require(a.level == 1, "entertainment to general preserves risk start");
    a = fusion.update(5000, evidence(5000, 1.0), evidence(5000, 0.0), phone(5000, PhoneCategory::GeneralOrUnknown));
    require(a.level == 2 && a.would_remind, "continuous risky phone reaches level two once");
    a = fusion.update(5100, evidence(5100, 1.0), evidence(5100, 0.0), phone(5100, PhoneCategory::Entertainment));
    require(a.level == 2 && !a.would_remind, "category switch does not duplicate reminder");
    a = fusion.update(5200, evidence(5200, 1.0), evidence(5200, 0.0), phone(5200, PhoneCategory::Learning));
    require(a.level == 0, "learning clears phone risk");
    fusion.reset();
    a = fusion.update(0, evidence(0, 1.0), evidence(0, 0.0), phone(0, std::nullopt));
    require(a.applicability == Applicability::Active && a.level == 0,
            "full reset clears timestamps and prior phone episode");

    FusionCoreCpp absence;
    auto b = absence.update(0, evidence(0, 0.0), std::nullopt, phone(0, std::nullopt));
    require(b.applicability == Applicability::Unknown, "absence waits for confirmation");
    b = absence.update(1000, evidence(1000, 0.0), std::nullopt, phone(1000, std::nullopt));
    require(b.applicability == Applicability::Unknown, "absence remains candidate before boundary");
    b = absence.update(1500, evidence(1500, 0.0), std::nullopt, phone(1500, std::nullopt));
    require(b.applicability == Applicability::NotPresent, "absence confirms at inclusive boundary");
    std::cout << "fusion tests passed\n";
    return 0;
}
