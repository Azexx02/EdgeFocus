#include "device/OpenCvFaceTargetSource.h"

#include <cassert>
#include <cmath>

using namespace edgefocus;

namespace {

void assertNear(float actual, float expected) {
    assert(std::fabs(actual - expected) < 0.0001f);
}

void test_target_from_face_rect_normalizes_frame_coordinates() {
    const auto target = OpenCvFaceTargetSource::targetFromFaceRect(
        640,
        480,
        160.0f,
        120.0f,
        160.0f,
        120.0f,
        0.86f);

    assert(target.valid);
    assertNear(target.center_x, 0.375f);
    assertNear(target.center_y, 0.375f);
    assertNear(target.width, 0.25f);
    assertNear(target.height, 0.25f);
    assertNear(target.confidence, 0.86f);
}

void test_target_from_invalid_rect_is_ignored() {
    const auto target = OpenCvFaceTargetSource::targetFromFaceRect(
        0, 480, 0.0f, 0.0f, 120.0f, 120.0f, 0.9f);
    assert(!target.valid);
}

} // namespace

int main() {
    test_target_from_face_rect_normalizes_frame_coordinates();
