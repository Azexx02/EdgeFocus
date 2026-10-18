#pragma once

#include <cstdint>
#include <vector>

namespace edgefocus {

struct H264NalUnit {
    std::vector<std::uint8_t> bytes;

    std::uint8_t type() const {
        return bytes.empty() ? 0 : static_cast<std::uint8_t>(bytes.front() & 0x1f);
    }
};

struct H264Sample {
    bool keyframe{};
    std::vector<std::uint8_t> avcc_bytes;
    std::vector<std::uint8_t> sps;
    std::vector<std::uint8_t> pps;
};

std::vector<H264NalUnit> splitAnnexBNalUnits(
    const std::vector<std::uint8_t>& annex_b);
H264Sample makeH264Sample(const std::vector<std::uint8_t>& annex_b,
                          bool keyframe);

class FragmentedMp4Muxer {
public:
    FragmentedMp4Muxer(int width, int height, std::uint32_t timescale,
                       std::uint32_t sample_duration);

    void reset();
    bool ready() const;
    bool configure(const std::vector<std::uint8_t>& sps,
                   const std::vector<std::uint8_t>& pps);
    const std::vector<std::uint8_t>& initializationSegment() const;
    std::vector<std::uint8_t> mediaFragment(
        const std::vector<std::uint8_t>& avcc_sample, bool keyframe);

private:
    int width_{};
    int height_{};
    std::uint32_t timescale_{};
    std::uint32_t sample_duration_{};
    std::uint32_t fragment_sequence_{1};
    std::uint64_t decode_time_{};
    std::vector<std::uint8_t> initialization_segment_;
};

} // namespace edgefocus

