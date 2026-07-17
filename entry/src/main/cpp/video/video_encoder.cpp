#include "video_encoder.h"
#include "util/DebugLog.h"

#include <chrono>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

// 硬编码的 SPS/PPS 数据（Annex-B 格式，包含起始码）
static char my_pps[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xC0, 0x1F, 0x8D, 0x8D, 0x50, 0x28, 0x02, 0xD9, 0x08, 0x00, 0x00, 0x03, 0x00,
    0x08, 0x00, 0x00, 0x03, 0x03, 0xC4, 0x78, 0x44, 0x23, 0x50, 0x00, 0x00, 0x00, 0x01, 0x68, 0xCE, 0x31, 0xB2
};

// 调试用文件写入（当前被注释，保留原样）
static FILE *g_encoderDump = nullptr;
static std::mutex g_dumpMutex;

// ============================================================
// VideoEncoder 实现
// ============================================================
VideoEncoder::VideoEncoder() : outputQueue_(std::make_shared<EncodedDataQueue>()) {}

VideoEncoder::~VideoEncoder() {
    Stop();
    Cleanup();
}

bool VideoEncoder::Init(int32_t w, int32_t h, int32_t kbps, int32_t fps, const std::string &mime) {
    std::lock_guard<std::mutex> lk(mutex_);
    width_ = w;
    height_ = h;

    encoder_ = OH_VideoEncoder_CreateByMime(mime.c_str());
    if (!encoder_) {
        LOGE("Create encoder failed");
        return false;
    }

    OH_AVCodecCallback cb = {OnError, OnStreamChanged, OnNeedInputBuffer, OnNewOutputBuffer};
    if (OH_VideoEncoder_RegisterCallback(encoder_, cb, this) != AV_ERR_OK) {
        Cleanup();
        return false;
    }

    OH_AVFormat *fmt = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_WIDTH, w);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_HEIGHT, h);

    // 【关键修改】：Surface输入模式下，必须设置为 AV_PIXEL_FORMAT_SURFACE_FORMAT
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_RGBA);

    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_BITRATE, kbps);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_FRAME_RATE, fps);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_I_FRAME_INTERVAL, 1000);

    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, OH_BitrateMode::BITRATE_MODE_CBR);
    if (OH_VideoEncoder_Configure(encoder_, fmt) != AV_ERR_OK) {
        OH_AVFormat_Destroy(fmt);
        Cleanup();
        return false;
    }
    OH_AVFormat_Destroy(fmt);

    if (OH_VideoEncoder_GetSurface(encoder_, &inputSurface_) != AV_ERR_OK || !inputSurface_) {
        Cleanup();
        return false;
    }
    LOGI("Encoder init: %dx%d %dkbps %dfps", w, h, kbps, fps);
    return true;
}

bool VideoEncoder::Start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (OH_VideoEncoder_Prepare(encoder_) != AV_ERR_OK)
        return false;
    if (OH_VideoEncoder_Start(encoder_) != AV_ERR_OK)
        return false;
    running_.store(true);
    ptsCounter_.store(0);
    hasSentCodecConfig_ = false;

    // 尝试从输出格式获取 codec_config
    OH_AVFormat *outFmt = OH_VideoEncoder_GetOutputDescription(encoder_);
    if (outFmt) {
        uint8_t *data = nullptr;
        size_t size = 0;
        if (OH_AVFormat_GetBuffer(outFmt, OH_MD_KEY_CODEC_CONFIG, &data, &size) && data && size > 0) {
            std::lock_guard<std::mutex> cl(codecMutex_);
            codecConfig_.assign(data, data + size);
            LOGI("Got codec_config from output format: %zu bytes", size);
        }
        OH_AVFormat_Destroy(outFmt);
    }
    LOGI("Encoder started");
    return true;
}

void VideoEncoder::Stop() {
    if (!running_.exchange(false))
        return;
    outputQueue_->Stop();

    std::lock_guard<std::mutex> lk(mutex_);
    if (encoder_) {
        OH_VideoEncoder_Stop(encoder_);
        OH_VideoEncoder_Reset(encoder_);
    }
    LOGI("Encoder stopped");
}

OHNativeWindow *VideoEncoder::GetInputSurface() const { return inputSurface_; }
std::shared_ptr<EncodedDataQueue> VideoEncoder::GetOutputQueue() const { return outputQueue_; }
bool VideoEncoder::IsRunning() const { return running_.load(); }

std::vector<uint8_t> VideoEncoder::GetCodecConfig() const {
    std::lock_guard<std::mutex> lk(codecMutex_);
    return codecConfig_;
}

void VideoEncoder::Cleanup() {
    if (encoder_) {
        OH_VideoEncoder_Destroy(encoder_);
        encoder_ = nullptr;
        inputSurface_ = nullptr;
    }
}

void VideoEncoder::OnError(OH_AVCodec *, int32_t e, void *) { LOGE("Encoder err=%d", e); }
void VideoEncoder::OnStreamChanged(OH_AVCodec *, OH_AVFormat *, void *) {}
void VideoEncoder::OnNeedInputBuffer(OH_AVCodec *, uint32_t, OH_AVBuffer *, void *) {}

// ============================================================
// 编码器输出回调 — 关键：提取或构造 codec_config
// ============================================================
void VideoEncoder::OnNewOutputBuffer(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *buffer, void *userData) {
    auto *enc = static_cast<VideoEncoder *>(userData);
    if (!enc) return;

    std::lock_guard<std::mutex> lk(enc->mutex_);

    if (!enc->running_.load() || !enc->encoder_) {
        if (enc->encoder_) {
            OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index);
        }
        return;
    }

    OH_AVCodecBufferAttr attr;
    if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK) {
        OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index);
        return;
    }
    uint8_t *data = OH_AVBuffer_GetAddr(buffer);
    if (!data || attr.size <= 0) {
        OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index);
        return;
    }

    bool isKey = (attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) != 0;
    bool isConfig = (attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) != 0;
    int64_t pts = enc->ptsCounter_.fetch_add(33333);

    LOGI("Enc out: sz=%d pts=%lld key=%d cfg=%d flags=0x%x [%02x %02x %02x %02x %02x]", 
         attr.size, (long long)pts, isKey, isConfig, attr.flags,
         data[0], data[1], data[2], data[3], data[4]);

    // //     // {
// //     //     std::lock_guard<std::mutex> lock(g_dumpMutex);
// //     //     if (!g_encoderDump) {
// //     //         // 尝试打开文件（路径可改为应用缓存目录，例如 /data/data/com.your.app/cache/encoder.h264）
// //     //         const char* path = "/data/storage/el2/base/haps/entry/cache/encoder2.h264";  // 需要 adb shell 权限
// //     //         // 或者使用应用私有目录: /data/data/<package>/cache/encoder.h264
// //     //         g_encoderDump = fopen(path, "wb");
// //     //         if (g_encoderDump) {
// //     //             LOGI("Opened file encoder dump file: %s", path);
// //     //         } else {
// //     //             LOGE("Opened file Failed to open dump file: %s", path);
// //     //         }
// //     //     }
// //     //     if (g_encoderDump) {
// //     //         fwrite(data, 1, attr.size, g_encoderDump);
// //     //         fflush(g_encoderDump);
// //     //         LOGI("Opened file %d bytes to encoder.h264", attr.size);
// //     //     }
// //     // }
    // ---- 关键帧强制插入硬编码配置包 ----
    if (isKey) {
        auto cpkt = std::make_unique<EncodedDataQueue::Packet>();
        cpkt->data.assign(my_pps, my_pps + sizeof(my_pps));
        cpkt->pts = 0;
        cpkt->isKeyFrame = false;
        cpkt->isCodecConfig = true;
        enc->outputQueue_->Push(std::move(cpkt));
        LOGI("Pushed hardcoded codec_config for keyframe, size=%zu", sizeof(my_pps));
        enc->hasSentCodecConfig_ = true;
    }

    // ---- 推送当前帧 ----
    auto pkt = std::make_unique<EncodedDataQueue::Packet>();
    pkt->data.assign(data, data + attr.size);
    pkt->pts = pts;
    pkt->isKeyFrame = isKey;
    pkt->isCodecConfig = isConfig;
    enc->outputQueue_->Push(std::move(pkt));

    // ---- 释放输出缓冲区 ----
    OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index);
}