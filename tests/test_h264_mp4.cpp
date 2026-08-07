#include "network/FragmentedMp4Muxer.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

bool containsBox(const std::vector<std::uint8_t>& bytes, const char* type) {
    const std::string text(bytes.begin(), bytes.end());
    return text.find(type) != std::string::npos;
}

std::uint32_t readU32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes.at(offset)) << 24) |
           (static_cast<std::uint32_t>(bytes.at(offset + 1)) << 16) |
           (static_cast<std::uint32_t>(bytes.at(offset + 2)) << 8) |
           static_cast<std::uint32_t>(bytes.at(offset + 3));
}

} // namespace

int main() {
    const std::vector<std::uint8_t> annex_b{
        0, 0, 0, 1, 0x67, 0x42, 0x00, 0x1f, 0xe5, 0x88,
        0, 0, 1, 0x68, 0xce, 0x38, 0x80,
        0, 0, 0, 1, 0x65, 0x88, 0x84, 0x21};
    const auto nals = edgefocus::splitAnnexBNalUnits(annex_b);
    assert(nals.size() == 3);
    assert(nals[0].type() == 7);
    assert(nals[1].type() == 8);
    assert(nals[2].type() == 5);

    const auto sample = edgefocus::makeH264Sample(annex_b, true);
    assert(sample.sps == nals[0].bytes);
    assert(sample.pps == nals[1].bytes);
    assert(readU32(sample.avcc_bytes, 0) == nals[2].bytes.size());

    edgefocus::FragmentedMp4Muxer muxer(640, 480, 90000, 6000);
    assert(muxer.configure(sample.sps, sample.pps));
    const auto& init = muxer.initializationSegment();
    assert(containsBox(init, "ftyp"));
    assert(containsBox(init, "moov"));
    assert(containsBox(init, "avcC"));
    assert(containsBox(init, "mvex"));

    const auto fragment = muxer.mediaFragment(sample.avcc_bytes, true);
    assert(containsBox(fragment, "moof"));
    assert(containsBox(fragment, "tfdt"));
    assert(containsBox(fragment, "mdat"));
    assert(readU32(fragment, 0) > 8);
    return 0;
}

