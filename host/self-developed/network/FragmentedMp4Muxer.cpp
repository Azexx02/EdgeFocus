#include "network/FragmentedMp4Muxer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace edgefocus {
namespace {

class Bytes {
public:
    std::size_t beginBox(std::string_view type) {
        const auto offset = data.size();
        u32(0);
        for (char ch : type) data.push_back(static_cast<std::uint8_t>(ch));
        return offset;
    }

    void endBox(std::size_t offset) {
        patchU32(offset, static_cast<std::uint32_t>(data.size() - offset));
    }

    void u8(std::uint8_t value) { data.push_back(value); }
    void u16(std::uint16_t value) {
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value));
    }
    void u24(std::uint32_t value) {
        data.push_back(static_cast<std::uint8_t>(value >> 16));
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value));
    }
    void u32(std::uint32_t value) {
        data.push_back(static_cast<std::uint8_t>(value >> 24));
        data.push_back(static_cast<std::uint8_t>(value >> 16));
        data.push_back(static_cast<std::uint8_t>(value >> 8));
        data.push_back(static_cast<std::uint8_t>(value));
    }
    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value >> 32));
        u32(static_cast<std::uint32_t>(value));
    }
    void zeros(std::size_t count) { data.insert(data.end(), count, 0); }
    void text(std::string_view value) {
        data.insert(data.end(), value.begin(), value.end());
    }
    void bytes(const std::vector<std::uint8_t>& value) {
        data.insert(data.end(), value.begin(), value.end());
    }
    void patchU32(std::size_t offset, std::uint32_t value) {
        data.at(offset) = static_cast<std::uint8_t>(value >> 24);
        data.at(offset + 1) = static_cast<std::uint8_t>(value >> 16);
        data.at(offset + 2) = static_cast<std::uint8_t>(value >> 8);
        data.at(offset + 3) = static_cast<std::uint8_t>(value);
    }

    std::vector<std::uint8_t> data;
};

void fullBox(Bytes& out, std::uint8_t version, std::uint32_t flags) {
    out.u8(version);
    out.u24(flags);
}

void matrix(Bytes& out) {
    constexpr std::array<std::uint32_t, 9> values{
        0x00010000, 0, 0, 0, 0x00010000, 0, 0, 0, 0x40000000};
    for (const auto value : values) out.u32(value);
}

std::vector<std::uint8_t> makeInitializationSegment(
    int width, int height, std::uint32_t timescale,
    const std::vector<std::uint8_t>& sps,
    const std::vector<std::uint8_t>& pps) {
    Bytes out;
    auto box = out.beginBox("ftyp");
    out.text("isom");
    out.u32(0x00000200);
    out.text("isom");
    out.text("iso6");
    out.text("avc1");
    out.text("mp41");
    out.endBox(box);

    const auto moov = out.beginBox("moov");
    box = out.beginBox("mvhd");
    fullBox(out, 0, 0);
    out.u32(0); out.u32(0);
    out.u32(timescale); out.u32(0);
    out.u32(0x00010000); out.u16(0x0100); out.u16(0);
    out.zeros(8); matrix(out); out.zeros(24); out.u32(2);
    out.endBox(box);

    const auto trak = out.beginBox("trak");
    box = out.beginBox("tkhd");
    fullBox(out, 0, 0x000007);
    out.u32(0); out.u32(0); out.u32(1); out.u32(0); out.u32(0);
    out.zeros(8); out.u16(0); out.u16(0); out.u16(0); out.u16(0);
    matrix(out);
    out.u32(static_cast<std::uint32_t>(width) << 16);
    out.u32(static_cast<std::uint32_t>(height) << 16);
    out.endBox(box);

    const auto mdia = out.beginBox("mdia");
    box = out.beginBox("mdhd");
    fullBox(out, 0, 0);
    out.u32(0); out.u32(0); out.u32(timescale); out.u32(0);
    out.u16(0x55c4); out.u16(0);
    out.endBox(box);
    box = out.beginBox("hdlr");
    fullBox(out, 0, 0); out.u32(0); out.text("vide"); out.zeros(12);
    out.text("EdgeFocus AVC"); out.u8(0);
    out.endBox(box);

    const auto minf = out.beginBox("minf");
    box = out.beginBox("vmhd");
    fullBox(out, 0, 1); out.u16(0); out.u16(0); out.u16(0); out.u16(0);
    out.endBox(box);
    const auto dinf = out.beginBox("dinf");
    const auto dref = out.beginBox("dref");
    fullBox(out, 0, 0); out.u32(1);
    box = out.beginBox("url "); fullBox(out, 0, 1); out.endBox(box);
    out.endBox(dref); out.endBox(dinf);

    const auto stbl = out.beginBox("stbl");
    const auto stsd = out.beginBox("stsd");
    fullBox(out, 0, 0); out.u32(1);
    const auto avc1 = out.beginBox("avc1");
    out.zeros(6); out.u16(1); out.zeros(16);
    out.u16(static_cast<std::uint16_t>(width));
    out.u16(static_cast<std::uint16_t>(height));
    out.u32(0x00480000); out.u32(0x00480000); out.u32(0);
    out.u16(1); out.zeros(32); out.u16(0x0018); out.u16(0xffff);
    box = out.beginBox("avcC");
    out.u8(1);
    out.u8(sps.size() > 1 ? sps[1] : 66);
    out.u8(sps.size() > 2 ? sps[2] : 0);
    out.u8(sps.size() > 3 ? sps[3] : 30);
    out.u8(0xff); out.u8(0xe1);
    out.u16(static_cast<std::uint16_t>(sps.size())); out.bytes(sps);
    out.u8(1); out.u16(static_cast<std::uint16_t>(pps.size())); out.bytes(pps);
    out.endBox(box);
    out.endBox(avc1); out.endBox(stsd);

    for (std::string_view type : {"stts", "stsc"}) {
        box = out.beginBox(type); fullBox(out, 0, 0); out.u32(0); out.endBox(box);
    }
    box = out.beginBox("stsz");
    fullBox(out, 0, 0); out.u32(0); out.u32(0); out.endBox(box);
    box = out.beginBox("stco");
    fullBox(out, 0, 0); out.u32(0); out.endBox(box);
    out.endBox(stbl); out.endBox(minf); out.endBox(mdia); out.endBox(trak);

    const auto mvex = out.beginBox("mvex");
    box = out.beginBox("trex");
    fullBox(out, 0, 0);
    out.u32(1); out.u32(1); out.u32(0); out.u32(0); out.u32(0);
    out.endBox(box); out.endBox(mvex); out.endBox(moov);
    return std::move(out.data);
}

std::size_t startCodeLength(const std::vector<std::uint8_t>& bytes,
                            std::size_t offset) {
    if (offset + 3 <= bytes.size() && bytes[offset] == 0 && bytes[offset + 1] == 0 &&
        bytes[offset + 2] == 1) return 3;
    if (offset + 4 <= bytes.size() && bytes[offset] == 0 && bytes[offset + 1] == 0 &&
        bytes[offset + 2] == 0 && bytes[offset + 3] == 1) return 4;
    return 0;
}

} // namespace

std::vector<H264NalUnit> splitAnnexBNalUnits(
    const std::vector<std::uint8_t>& annex_b) {
    std::vector<H264NalUnit> result;
    std::size_t cursor = 0;
    while (cursor < annex_b.size()) {
        while (cursor < annex_b.size() && startCodeLength(annex_b, cursor) == 0) ++cursor;
        if (cursor >= annex_b.size()) break;
        const auto code = startCodeLength(annex_b, cursor);
        const auto begin = cursor + code;
        auto end = begin;
        while (end < annex_b.size() && startCodeLength(annex_b, end) == 0) ++end;
        if (end > begin) {
            result.push_back({std::vector<std::uint8_t>(annex_b.begin() + begin,
                                                       annex_b.begin() + end)});
        }
        cursor = end;
    }
    return result;
}

H264Sample makeH264Sample(const std::vector<std::uint8_t>& annex_b,
                          bool keyframe) {
    H264Sample sample;
    sample.keyframe = keyframe;
    for (const auto& nal : splitAnnexBNalUnits(annex_b)) {
        if (nal.type() == 7) {
            sample.sps = nal.bytes;
            continue;
        }
        if (nal.type() == 8) {
            sample.pps = nal.bytes;
            continue;
        }
        if (nal.bytes.empty()) continue;
        const auto size = static_cast<std::uint32_t>(nal.bytes.size());
        sample.avcc_bytes.push_back(static_cast<std::uint8_t>(size >> 24));
        sample.avcc_bytes.push_back(static_cast<std::uint8_t>(size >> 16));
        sample.avcc_bytes.push_back(static_cast<std::uint8_t>(size >> 8));
        sample.avcc_bytes.push_back(static_cast<std::uint8_t>(size));
        sample.avcc_bytes.insert(sample.avcc_bytes.end(), nal.bytes.begin(), nal.bytes.end());
    }
    return sample;
}

FragmentedMp4Muxer::FragmentedMp4Muxer(
    int width, int height, std::uint32_t timescale,
    std::uint32_t sample_duration)
    : width_(width), height_(height), timescale_(timescale),
      sample_duration_(sample_duration) {}

void FragmentedMp4Muxer::reset() {
    fragment_sequence_ = 1;
    decode_time_ = 0;
    initialization_segment_.clear();
}

bool FragmentedMp4Muxer::ready() const {
    return !initialization_segment_.empty();
}

bool FragmentedMp4Muxer::configure(
    const std::vector<std::uint8_t>& sps,
    const std::vector<std::uint8_t>& pps) {
    if (width_ <= 0 || height_ <= 0 || sps.size() < 4 || pps.empty()) return false;
    initialization_segment_ = makeInitializationSegment(
        width_, height_, timescale_, sps, pps);
    fragment_sequence_ = 1;
    decode_time_ = 0;
    return true;
}

const std::vector<std::uint8_t>& FragmentedMp4Muxer::initializationSegment() const {
    return initialization_segment_;
}

std::vector<std::uint8_t> FragmentedMp4Muxer::mediaFragment(
    const std::vector<std::uint8_t>& avcc_sample, bool keyframe) {
    if (!ready() || avcc_sample.empty()) return {};
    Bytes out;
    const auto moof = out.beginBox("moof");
    auto box = out.beginBox("mfhd");
    fullBox(out, 0, 0); out.u32(fragment_sequence_++); out.endBox(box);
    const auto traf = out.beginBox("traf");
    box = out.beginBox("tfhd");
    fullBox(out, 0, 0x020000); out.u32(1); out.endBox(box);
    box = out.beginBox("tfdt");
    fullBox(out, 1, 0); out.u64(decode_time_); out.endBox(box);
    const auto trun = out.beginBox("trun");
    fullBox(out, 0, 0x000701); out.u32(1);
    const auto data_offset_location = out.data.size();
    out.u32(0);
    out.u32(sample_duration_);
    out.u32(static_cast<std::uint32_t>(avcc_sample.size()));
    out.u32(keyframe ? 0x02000000 : 0x01010000);
    out.endBox(trun); out.endBox(traf); out.endBox(moof);
    out.patchU32(data_offset_location,
                 static_cast<std::uint32_t>(out.data.size() + 8));
    box = out.beginBox("mdat"); out.bytes(avcc_sample); out.endBox(box);
    decode_time_ += sample_duration_;
    return std::move(out.data);
}

} // namespace edgefocus
