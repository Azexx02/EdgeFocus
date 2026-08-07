#include "network/H264PreviewEncoder.h"

#include "network/FragmentedMp4Muxer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>

#if defined(EDGEFOCUS_WITH_OPENH264) && defined(EDGEFOCUS_WITH_OPENCV)
#include <dlfcn.h>
#include <opencv2/imgproc.hpp>
#include <wels/codec_api.h>
#endif

namespace edgefocus {
namespace {

std::int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

struct H264PreviewEncoder::Impl {
    H264PreviewEncoderHealth health;
#if defined(EDGEFOCUS_WITH_OPENH264) && defined(EDGEFOCUS_WITH_OPENCV)
    ISVCEncoder* encoder{};
    void* library{};
    using CreateEncoder = int (*)(ISVCEncoder**);
    using DestroyEncoder = void (*)(ISVCEncoder*);
    CreateEncoder create_encoder{};
    DestroyEncoder destroy_encoder{};
    std::unique_ptr<FragmentedMp4Muxer> muxer;
    int width{};
    int height{};
    bool force_keyframe{};
    std::vector<std::uint8_t> i420;

    Impl() { health.compiled = true; }

    ~Impl() {
        close();
        if (library) ::dlclose(library);
    }

    void close() {
        if (encoder) {
            encoder->Uninitialize();
            if (destroy_encoder) destroy_encoder(encoder);
            encoder = nullptr;
        }
        muxer.reset();
        width = 0;
        height = 0;
        health.initialized = false;
    }

    bool loadLibrary() {
        if (library && create_encoder && destroy_encoder) return true;
        std::array<const char*, 4> candidates{
            std::getenv("EDGEFOCUS_OPENH264_LIBRARY"),
            "/opt/edgefocus/lib/libopenh264.so.8",
            "libopenh264.so.8",
            "libopenh264.so"};
        for (const char* candidate : candidates) {
            if (!candidate || !*candidate) continue;
            library = ::dlopen(candidate, RTLD_NOW | RTLD_LOCAL);
            if (library) break;
        }
        if (!library) {
            const char* detail = ::dlerror();
            health.error = std::string("OpenH264 library unavailable") +
                (detail ? std::string(": ") + detail : std::string());
            return false;
        }
        create_encoder = reinterpret_cast<CreateEncoder>(
            ::dlsym(library, "WelsCreateSVCEncoder"));
        destroy_encoder = reinterpret_cast<DestroyEncoder>(
            ::dlsym(library, "WelsDestroySVCEncoder"));
        if (!create_encoder || !destroy_encoder) {
            health.error = "OpenH264 library has no encoder API";
            ::dlclose(library);
            library = nullptr;
            return false;
        }
        return true;
    }

    bool open(int requested_width, int requested_height) {
        close();
        width = requested_width;
        height = requested_height;
        if (!loadLibrary()) return false;
        if (create_encoder(&encoder) != 0 || !encoder) {
            health.error = "WelsCreateSVCEncoder failed";
            return false;
        }

        SEncParamExt params{};
        if (encoder->GetDefaultParams(&params) != 0) {
            health.error = "OpenH264 GetDefaultParams failed";
            close();
            return false;
        }
        constexpr int kFps = 15;
        constexpr int kBitrate = 650'000;
        params.iUsageType = CAMERA_VIDEO_REAL_TIME;
        params.iPicWidth = width;
        params.iPicHeight = height;
        params.iTargetBitrate = kBitrate;
        params.iRCMode = RC_BITRATE_MODE;
        params.fMaxFrameRate = static_cast<float>(kFps);
        params.iTemporalLayerNum = 1;
        params.iSpatialLayerNum = 1;
        params.iComplexityMode = LOW_COMPLEXITY;
        params.uiIntraPeriod = kFps;
        params.iNumRefFrame = 1;
        params.iEntropyCodingModeFlag = 0;
        params.bEnableFrameSkip = true;
        params.iMultipleThreadIdc = 1;
        params.bEnableDenoise = false;
        params.bEnableBackgroundDetection = false;
        params.bEnableAdaptiveQuant = false;
        auto& layer = params.sSpatialLayers[0];
        layer.iVideoWidth = width;
        layer.iVideoHeight = height;
        layer.fFrameRate = static_cast<float>(kFps);
        layer.iSpatialBitrate = kBitrate;
        layer.iMaxSpatialBitrate = 900'000;
        layer.uiProfileIdc = PRO_BASELINE;
        layer.uiLevelIdc = LEVEL_3_0;
        layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;

        if (encoder->InitializeExt(&params) != 0) {
            health.error = "OpenH264 InitializeExt failed";
            close();
            return false;
        }
        int format = videoFormatI420;
        if (encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &format) != 0) {
            health.error = "OpenH264 I420 configuration failed";
            close();
            return false;
        }
        muxer = std::make_unique<FragmentedMp4Muxer>(width, height, 90000, 6000);
        i420.resize(static_cast<std::size_t>(width * height * 3 / 2));
        health.initialized = true;
        health.healthy = true;
        health.consecutive_failures = 0;
        health.error.clear();
        force_keyframe = true;
        return true;
    }

    bool convert(const CameraFrame& frame) {
        if (!frame.valid()) return false;
        cv::Mat source;
        if (frame.channels == 3) {
            const int type = CV_8UC3;
            cv::Mat packed(frame.height, frame.width, type,
                           const_cast<std::uint8_t*>(frame.data.data()));
            if (frame.format == PixelFormat::RGB24) {
                cv::cvtColor(packed, source, cv::COLOR_RGB2BGR);
            } else {
                source = packed;
            }
        } else if (frame.channels == 1 && frame.format == PixelFormat::GRAY8) {
            cv::Mat gray(frame.height, frame.width, CV_8UC1,
                         const_cast<std::uint8_t*>(frame.data.data()));
            cv::cvtColor(gray, source, cv::COLOR_GRAY2BGR);
        } else {
            return false;
        }
        cv::Mat resized;
        if (source.cols != width || source.rows != height) {
            cv::resize(source, resized, cv::Size(width, height), 0, 0, cv::INTER_AREA);
        } else {
            resized = source;
        }
        cv::Mat yuv(height * 3 / 2, width, CV_8UC1, i420.data());
        cv::cvtColor(resized, yuv, cv::COLOR_BGR2YUV_I420);
        return true;
    }

    bool encodeFrame(const CameraFrame& frame, const std::string& source_key,
                     std::uint64_t sequence, H264PreviewPacket& output) {
        const int target_width = std::max(2, std::min(384, frame.width) & ~1);
        const int scaled_height = static_cast<int>(
            static_cast<double>(frame.height) * target_width / std::max(1, frame.width));
        const int target_height = std::max(2, scaled_height & ~1);
        if (!encoder || width != target_width || height != target_height) {
            if (!open(target_width, target_height)) return false;
        }
        if (!convert(frame)) {
            health.error = "unsupported preview pixel format";
            return false;
        }
        if (force_keyframe) {
            encoder->ForceIntraFrame(true);
            force_keyframe = false;
        }

        SSourcePicture picture{};
        picture.iColorFormat = videoFormatI420;
        picture.iPicWidth = width;
        picture.iPicHeight = height;
        picture.iStride[0] = width;
        picture.iStride[1] = width / 2;
        picture.iStride[2] = width / 2;
        picture.pData[0] = i420.data();
        picture.pData[1] = i420.data() + width * height;
        picture.pData[2] = picture.pData[1] + width * height / 4;
        picture.uiTimeStamp = frame.timestamp_ms;

        SFrameBSInfo info{};
        if (encoder->EncodeFrame(&picture, &info) != 0) {
            health.error = "OpenH264 EncodeFrame failed";
            return false;
        }
        if (info.eFrameType == videoFrameTypeSkip) return true;
        std::vector<std::uint8_t> annex_b;
        annex_b.reserve(static_cast<std::size_t>(std::max(0, info.iFrameSizeInBytes)));
        for (int layer_index = 0; layer_index < info.iLayerNum; ++layer_index) {
            const auto& layer = info.sLayerInfo[layer_index];
            int offset = 0;
            for (int nal_index = 0; nal_index < layer.iNalCount; ++nal_index) {
                const int length = layer.pNalLengthInByte[nal_index];
                if (length <= 0) continue;
                annex_b.insert(annex_b.end(), layer.pBsBuf + offset,
                               layer.pBsBuf + offset + length);
                offset += length;
            }
        }
        const bool keyframe = info.eFrameType == videoFrameTypeIDR ||
                              info.eFrameType == videoFrameTypeI;
        auto sample = makeH264Sample(annex_b, keyframe);
        if (!sample.sps.empty() && !sample.pps.empty()) {
            muxer->configure(sample.sps, sample.pps);
        }
        if (!muxer->ready() || sample.avcc_bytes.empty()) {
            health.error = "OpenH264 produced no decodable sample";
            return false;
        }
        auto fragment = muxer->mediaFragment(sample.avcc_bytes, keyframe);
        if (fragment.empty()) {
            health.error = "fragmented MP4 mux failed";
            return false;
        }
        output = H264PreviewPacket{};
        output.source_key = source_key;
        output.sequence = sequence;
        output.timestamp_ms = frame.timestamp_ms;
        output.encoded_at_ms = steadyNowMs();
        output.width = width;
        output.height = height;
        output.keyframe = keyframe;
        output.initialization_segment = muxer->initializationSegment();
        output.media_fragment = std::move(fragment);
        health.healthy = true;
        health.consecutive_failures = 0;
        health.error.clear();
        return true;
    }
#else
    Impl() {
        health.compiled = false;
        health.error = "OpenH264 preview not compiled";
    }
#endif
};

H264PreviewEncoder::H264PreviewEncoder() : impl_(std::make_unique<Impl>()) {}
H264PreviewEncoder::~H264PreviewEncoder() = default;

bool H264PreviewEncoder::encode(
    const CameraFrame& frame, const std::string& source_key,
    std::uint64_t sequence, H264PreviewPacket& output) {
#if defined(EDGEFOCUS_WITH_OPENH264) && defined(EDGEFOCUS_WITH_OPENCV)
    const auto started = std::chrono::steady_clock::now();
    const bool ok = impl_->encodeFrame(frame, source_key, sequence, output);
    impl_->health.encode_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - started).count();
    if (!ok) {
        ++impl_->health.consecutive_failures;
        impl_->health.healthy = false;
    }
    return ok;
#else
    (void)frame; (void)source_key; (void)sequence; (void)output;
    return false;
#endif
}

void H264PreviewEncoder::reset() {
#if defined(EDGEFOCUS_WITH_OPENH264) && defined(EDGEFOCUS_WITH_OPENCV)
    impl_->close();
    impl_->health.healthy = false;
    impl_->health.consecutive_failures = 0;
    impl_->health.error.clear();
#endif
}

void H264PreviewEncoder::forceKeyframe() {
#if defined(EDGEFOCUS_WITH_OPENH264) && defined(EDGEFOCUS_WITH_OPENCV)
    impl_->force_keyframe = true;
#endif
}

H264PreviewEncoderHealth H264PreviewEncoder::health() const {
    return impl_->health;
}

} // namespace edgefocus
