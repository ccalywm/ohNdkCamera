#include "video_decoder.h"
#include "util/DebugLog.h"

#include <chrono>
#include <cstring>

// ============================================================
// VideoDecoder 实现
// ============================================================
VideoDecoder::VideoDecoder() {}

VideoDecoder::~VideoDecoder() {
    Stop();
    Cleanup();
}

bool VideoDecoder::Init(OHNativeWindow *surf, const std::string &mime, int32_t w, int32_t h,
                        const std::vector<uint8_t> &codecConfig) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_.load() || !surf)
        return false;
    outputSurface_ = surf;

    if (decoder_) {
        OH_VideoDecoder_Stop(decoder_);
        OH_VideoDecoder_Reset(decoder_);
        OH_VideoDecoder_Destroy(decoder_);
        decoder_ = nullptr;
    }

    decoder_ = OH_VideoDecoder_CreateByMime(mime.c_str());
    if (!decoder_) {
        LOGE("Create decoder failed");
        return false;
    }

    OH_AVCodecCallback cb = {OnError, OnStreamChanged, OnNeedInputBuffer, OnNewOutputBuffer};
    if (OH_VideoDecoder_RegisterCallback(decoder_, cb, this) != AV_ERR_OK) {
        OH_VideoDecoder_Destroy(decoder_);
        decoder_ = nullptr;
        return false;
    }

    OH_AVFormat *fmt = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_WIDTH, w);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_HEIGHT, h);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);

    // 设置 codec_config（如果提供）
    if (!codecConfig.empty()) {
        OH_AVFormat_SetBuffer(fmt, OH_MD_KEY_CODEC_CONFIG, const_cast<uint8_t *>(codecConfig.data()),
                              static_cast<int32_t>(codecConfig.size()));
        LOGI("Decoder: set codec_config in configure: %zu bytes", codecConfig.size());
    }

    if (OH_VideoDecoder_Configure(decoder_, fmt) != AV_ERR_OK) {
        LOGE("Decoder Configure failed");
        OH_AVFormat_Destroy(fmt);
        OH_VideoDecoder_Destroy(decoder_);
        decoder_ = nullptr;
        return false;
    }
    OH_AVFormat_Destroy(fmt);

    OH_AVErrCode err = OH_VideoDecoder_SetSurface(decoder_, outputSurface_);
    if (err != AV_ERR_OK) {
        LOGE("SetSurface err=%d", err);
        OH_VideoDecoder_Destroy(decoder_);
        decoder_ = nullptr;
        return false;
    }

    needKeyFrame_.store(true);
    stopped_.store(false);
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        while (!inputQueue_.empty())
            inputQueue_.pop();
    }

    LOGI("Decoder init OK");
    return true;
}

bool VideoDecoder::Start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_.load() || !decoder_)
        return false;
    if (OH_VideoDecoder_Prepare(decoder_) != AV_ERR_OK) {
        LOGE("Prepare fail");
        return false;
    }
    needKeyFrame_.store(true);
    if (OH_VideoDecoder_Start(decoder_) != AV_ERR_OK) {
        LOGE("Start fail");
        return false;
    }
    running_.store(true);
    LOGI("Decoder started");
    return true;
}

void VideoDecoder::Stop() {
    if (!running_.exchange(false))
        return;
    stopped_.store(true);
    matchCond_.notify_all();
    std::lock_guard<std::mutex> lk(mutex_);
    if (decoder_) {
        OH_VideoDecoder_Stop(decoder_);
        OH_VideoDecoder_Reset(decoder_);
    }
    {
        std::lock_guard<std::mutex> m(matchMutex_);
        while (!inputQueue_.empty())
            inputQueue_.pop();
    }
    LOGI("Decoder stopped");
}

bool VideoDecoder::IsRunning() const { return running_.load(); }

bool VideoDecoder::QueuePacket(const uint8_t *data, size_t size, int64_t pts, bool isKeyFrame, bool isCodecConfig) {
    if (!running_.load() || stopped_.load() || !data || size == 0)
        return false;

    if (!isCodecConfig) {
        if (needKeyFrame_.load()) {
            if (!isKeyFrame)
                return true;
            needKeyFrame_.store(false);
            LOGI("Decoder got first keyframe, sz=%zu", size);
        }
    }

    auto pkt = std::make_unique<InputPacket>();
    pkt->data.assign(data, data + size);
    pkt->pts = pts;
    pkt->isKeyFrame = isKeyFrame;
    pkt->isCodecConfig = isCodecConfig;

    {
        std::lock_guard<std::mutex> lk(matchMutex_);
        inputQueue_.push(std::move(pkt));
    }
    matchCond_.notify_one();
    return true;
}

// ============================================================
// FillAndPush — 在回调内填充并提交
// ============================================================
void VideoDecoder::FillAndPush(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *buffer, const InputPacket &pkt) {
    int32_t cap = OH_AVBuffer_GetCapacity(buffer);
    uint8_t *dst = OH_AVBuffer_GetAddr(buffer);

    LOGI("FillAndPush: idx=%u cap=%d dst=%p sz=%zu cfg=%d key=%d", index, cap, dst, pkt.data.size(), pkt.isCodecConfig,
         pkt.isKeyFrame);

    if (!dst || cap <= 0) {
        LOGE("Buffer invalid!");
        return;
    }
    if ((size_t)cap < pkt.data.size()) {
        LOGE("Buffer too small!");
        return;
    }

    memcpy(dst, pkt.data.data(), pkt.data.size());

    OH_AVCodecBufferAttr attr = {};
    attr.pts = pkt.pts;
    attr.size = static_cast<int32_t>(pkt.data.size());
    attr.offset = 0;
    if (pkt.isCodecConfig) {
        attr.flags = AVCODEC_BUFFER_FLAGS_CODEC_DATA;
    } else if (pkt.isKeyFrame) {
        attr.flags = AVCODEC_BUFFER_FLAGS_SYNC_FRAME;
    } else {
        attr.flags = AVCODEC_BUFFER_FLAGS_NONE;
    }

    OH_AVBuffer_SetBufferAttr(buffer, &attr);
    OH_AVErrCode ret = OH_VideoDecoder_PushInputBuffer(codec, index);

    LOGI("PushInputData: ret=%d idx=%u sz=%d flags=0x%x", ret, index, attr.size, attr.flags);
}

// ============================================================
// OnNeedInputBuffer — 回调内等待数据并提交
// ============================================================
void VideoDecoder::OnNeedInputBuffer(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *buffer, void *userData) {
    auto *dec = static_cast<VideoDecoder *>(userData);
    if (!dec || dec->stopped_.load() || !buffer)
        return;

    LOGI("OnNeedInputBuffer: idx=%u", index);

    std::unique_ptr<InputPacket> pkt;
    {
        std::lock_guard<std::mutex> lk(dec->matchMutex_);
        if (!dec->inputQueue_.empty()) {
            pkt = std::move(dec->inputQueue_.front());
            dec->inputQueue_.pop();
        }
    }
    if (pkt) {
        dec->FillAndPush(codec, index, buffer, *pkt);
        return;
    }

    {
        std::unique_lock<std::mutex> lk(dec->matchMutex_);
        bool got = dec->matchCond_.wait_for(lk, std::chrono::milliseconds(500),
                                            [dec] { return !dec->inputQueue_.empty() || dec->stopped_.load(); });
        if (got && !dec->inputQueue_.empty() && !dec->stopped_.load()) {
            pkt = std::move(dec->inputQueue_.front());
            dec->inputQueue_.pop();
        }
    }

    if (pkt) {
        dec->FillAndPush(codec, index, buffer, *pkt);
    } else {
        LOGI("OnNeedInputBuffer: idx=%u timeout, releasing", index);
    }
}

void VideoDecoder::OnNewOutputBuffer(OH_AVCodec *codec, uint32_t index, OH_AVBuffer *buffer, void *userData) {
    auto *dec = static_cast<VideoDecoder *>(userData);
    if (!dec || dec->stopped_.load())
        return;

    OH_AVCodecBufferAttr attr;
    if (OH_AVBuffer_GetBufferAttr(buffer, &attr) == AV_ERR_OK) {
        LOGI("Dec output: idx=%u sz=%d pts=%lld", index, attr.size, (long long)attr.pts);
    }

    OH_AVErrCode ret = OH_VideoDecoder_RenderOutputBuffer(codec, index);
    if (ret != AV_ERR_OK) {
        LOGE("RenderOutput err=%d", ret);
        OH_VideoDecoder_FreeOutputBuffer(codec, index);
    }
}

void VideoDecoder::OnError(OH_AVCodec*, int32_t e, void*) { LOGE("Decoder err=%d", e); }
void VideoDecoder::OnStreamChanged(OH_AVCodec*, OH_AVFormat*, void*) {}

void VideoDecoder::Cleanup() {
    if (decoder_) {
        OH_VideoDecoder_Destroy(decoder_);
        decoder_ = nullptr;
        outputSurface_ = nullptr;
    }
}