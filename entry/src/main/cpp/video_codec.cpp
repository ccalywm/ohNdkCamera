#include "video_codec.h"
#include <chrono>
#include <cstring>

// ============================================================
// EncodedDataQueue
// ============================================================
void EncodedDataQueue::Push(std::unique_ptr<Packet> pkt) {
    if (stopped_.load()) return;
    { std::lock_guard<std::mutex> lk(mutex_); queue_.push(std::move(pkt)); }
    cond_.notify_one();
}
std::unique_ptr<EncodedDataQueue::Packet> EncodedDataQueue::Pop(int ms) {
    std::unique_lock<std::mutex> lk(mutex_);
    cond_.wait_for(lk, std::chrono::milliseconds(ms),
                   [this]{ return !queue_.empty() || stopped_.load(); });
    if (stopped_.load() || queue_.empty()) return nullptr;
    auto p = std::move(queue_.front()); queue_.pop(); return p;
}
void EncodedDataQueue::Stop() { stopped_.store(true); cond_.notify_all(); }
size_t EncodedDataQueue::Size() const {
    std::lock_guard<std::mutex> lk(mutex_); return queue_.size();
}

// ============================================================
// VideoEncoder
// ============================================================
VideoEncoder::VideoEncoder() : outputQueue_(std::make_shared<EncodedDataQueue>()) {}
VideoEncoder::~VideoEncoder() { Stop(); Cleanup(); }

bool VideoEncoder::Init(int32_t w, int32_t h, int32_t kbps, int32_t fps,
                        const std::string& mime) {
    std::lock_guard<std::mutex> lk(mutex_);
    width_ = w; height_ = h;

    encoder_ = OH_VideoEncoder_CreateByMime(mime.c_str());
    if (!encoder_) { LOGE("Create encoder failed"); return false; }

    OH_AVCodecCallback cb = {OnError, OnStreamChanged, OnNeedInputBuffer, OnNewOutputBuffer};
    if (OH_VideoEncoder_RegisterCallback(encoder_, cb, this) != AV_ERR_OK) {
        Cleanup(); return false;
    }

    OH_AVFormat* fmt = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_WIDTH, w);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_HEIGHT, h);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_NV12);
    // OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_BITRATE, kbps * 1000);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_FRAME_RATE, fps);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_I_FRAME_INTERVAL, 1);
    
    // 【关键修改 2】：使用 SURFACE_FORMAT 时，强烈建议显式指定编码 Profile，防止底层协商失败
    // OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PROFILE, AVC_PROFILE_BASELINE);
    
    if (OH_VideoEncoder_Configure(encoder_, fmt) != AV_ERR_OK) {
        OH_AVFormat_Destroy(fmt); Cleanup(); return false;
    }
    OH_AVFormat_Destroy(fmt);

    if (OH_VideoEncoder_GetSurface(encoder_, &inputSurface_) != AV_ERR_OK || !inputSurface_) {
        Cleanup(); return false;
    }
    LOGI("Encoder init: %dx%d %dkbps %dfps", w, h, kbps, fps);
    return true;
}

bool VideoEncoder::Start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (OH_VideoEncoder_Prepare(encoder_) != AV_ERR_OK) return false;
    if (OH_VideoEncoder_Start(encoder_) != AV_ERR_OK) return false;
    running_.store(true); ptsCounter_.store(0); hasSentCodecConfig_ = false;

    // ===== 尝试从输出格式获取 codec_config =====
    OH_AVFormat* outFmt = OH_VideoEncoder_GetOutputDescription(encoder_);
    if (outFmt) {
        uint8_t* data = nullptr;
        size_t  size = 0;
        if (OH_AVFormat_GetBuffer(outFmt, OH_MD_KEY_CODEC_CONFIG, &data, &size)
            && data && size > 0) {
            std::lock_guard<std::mutex> cl(codecMutex_);
            codecConfig_.assign(data, data + size);
            LOGI("Got codec_config from output format: %zu bytes", size);
        }
        OH_AVFormat_Destroy(outFmt);
    }
    LOGI("Encoder started"); return true;
}

void VideoEncoder::Stop() {
    if (!running_.exchange(false)) return;
    outputQueue_->Stop();
    std::lock_guard<std::mutex> lk(mutex_);
    if (encoder_) { OH_VideoEncoder_Stop(encoder_); OH_VideoEncoder_Reset(encoder_); }
}

OHNativeWindow* VideoEncoder::GetInputSurface() const { return inputSurface_; }
std::shared_ptr<EncodedDataQueue> VideoEncoder::GetOutputQueue() const { return outputQueue_; }
bool VideoEncoder::IsRunning() const { return running_.load(); }

std::vector<uint8_t> VideoEncoder::GetCodecConfig() const {
    std::lock_guard<std::mutex> lk(codecMutex_);
    return codecConfig_;
}

void VideoEncoder::Cleanup() {
    if (encoder_) { OH_VideoEncoder_Destroy(encoder_); encoder_ = nullptr; inputSurface_ = nullptr; }
}

void VideoEncoder::OnError(OH_AVCodec*, int32_t e, void*) { LOGE("Encoder err=%d", e); }
void VideoEncoder::OnStreamChanged(OH_AVCodec*, OH_AVFormat*, void*) {}
void VideoEncoder::OnNeedInputBuffer(OH_AVCodec*, uint32_t, OH_AVBuffer*, void*) {}

// ============================================================
// 编码器输出回调 — 关键：提取或构造 codec_config
// ============================================================
void VideoEncoder::OnNewOutputBuffer(OH_AVCodec* codec, uint32_t index,
                                      OH_AVBuffer* buffer, void* userData) {
    auto* enc = static_cast<VideoEncoder*>(userData);
    if (!enc->running_.load()) return;

    OH_AVCodecBufferAttr attr;
    if (OH_AVBuffer_GetBufferAttr(buffer, &attr) != AV_ERR_OK) {
        OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index); return;
    }
    uint8_t* data = OH_AVBuffer_GetAddr(buffer);
    if (!data || attr.size <= 0) {
        OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index); return;
    }

    bool isKey = (attr.flags & AVCODEC_BUFFER_FLAGS_SYNC_FRAME) != 0;
    bool isConfig = (attr.flags & AVCODEC_BUFFER_FLAGS_CODEC_DATA) != 0;
    int64_t pts = enc->ptsCounter_.fetch_add(33333);

    LOGI("Enc out: sz=%d pts=%lld key=%d cfg=%d flags=0x%x [%02x %02x %02x %02x]",
         attr.size, (long long)pts, isKey, isConfig, attr.flags,
         data[0], data[1], data[2], data[3]);

    // 第一个关键帧时，确保正确下发 codec_config
    if (isKey && !enc->hasSentCodecConfig_) {
        std::lock_guard<std::mutex> cl(enc->codecMutex_);
        if (enc->codecConfig_.empty()) {
            // 1. 尝试从输出格式获取
            OH_AVFormat* outFmt = OH_VideoEncoder_GetOutputDescription(codec);
            if (outFmt) {
                uint8_t* cfg = nullptr;
                size_t cfgSz = 0;
                if (OH_AVFormat_GetBuffer(outFmt, OH_MD_KEY_CODEC_CONFIG, &cfg, &cfgSz) && cfg && cfgSz > 0) {
                    enc->codecConfig_.assign(cfg, cfg + cfgSz);
                    LOGI("Got codec_config from format: %zu bytes", cfgSz);
                }
                OH_AVFormat_Destroy(outFmt);
            }

            // 2. 如果格式中没有，则从 IDR 帧码流中提取 (寻找 0x65 IDR NALU 的位置)
            if (enc->codecConfig_.empty()) {
                size_t idrOffset = 0;
                for (size_t i = 0; i + 4 < static_cast<size_t>(attr.size); ++i) {
                    // 寻找起始码 00 00 00 01
                    if (data[i] == 0x00 && data[i+1] == 0x00 && data[i+2] == 0x00 && data[i+3] == 0x01) {
                        uint8_t naluType = data[i+4] & 0x1F;
                        if (naluType == 5) { // 5 表示 IDR 帧
                            idrOffset = i;
                            break;
                        }
                    }
                }
                
                if (idrOffset > 0) {
                    // IDR 前面的部分即为 SPS 和 PPS 序列
                    enc->codecConfig_.assign(data, data + idrOffset);
                    LOGI("Extracted codec_config from stream: %zu bytes", idrOffset);
                } else {
                    LOGE("Failed to find SPS/PPS/IDR in keyframe!");
                }
            }
        }

        // 推送独立的配置帧
        if (!enc->codecConfig_.empty()) {
            auto cpkt = std::make_unique<EncodedDataQueue::Packet>();
            cpkt->data = enc->codecConfig_;
            cpkt->pts = 0;
            cpkt->isKeyFrame = false;
            cpkt->isCodecConfig = true; // 标记为配置帧
            enc->outputQueue_->Push(std::move(cpkt));
            LOGI(">>> Pushed codec_config: %zu bytes", cpkt->data.size());
            enc->hasSentCodecConfig_ = true;
        }
    }

    // 推送常规帧数据 (包含视频画面数据)
    auto pkt = std::make_unique<EncodedDataQueue::Packet>();
    pkt->data.assign(data, data + attr.size);
    pkt->pts = pts;
    pkt->isKeyFrame = isKey;
    pkt->isCodecConfig = isConfig;
    enc->outputQueue_->Push(std::move(pkt));

    OH_VideoEncoder_FreeOutputBuffer(enc->encoder_, index);
}

// ============================================================
// VideoDecoder
// ============================================================
VideoDecoder::VideoDecoder() {}
VideoDecoder::~VideoDecoder() { Stop(); Cleanup(); }

bool VideoDecoder::Init(OHNativeWindow* surf, const std::string& mime, int32_t w, int32_t h,
                        const std::vector<uint8_t>& codecConfig) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_.load() || !surf) return false;
    outputSurface_ = surf;

    if (decoder_) {
        OH_VideoDecoder_Stop(decoder_);
        OH_VideoDecoder_Reset(decoder_);
        OH_VideoDecoder_Destroy(decoder_);
        decoder_ = nullptr;
    }

    decoder_ = OH_VideoDecoder_CreateByMime(mime.c_str());
    if (!decoder_) { LOGE("Create decoder failed"); return false; }

    OH_AVCodecCallback cb = {OnError, OnStreamChanged, OnNeedInputBuffer, OnNewOutputBuffer};
    if (OH_VideoDecoder_RegisterCallback(decoder_, cb, this) != AV_ERR_OK) {
        OH_VideoDecoder_Destroy(decoder_); decoder_ = nullptr; return false;
    }

    OH_AVFormat* fmt = OH_AVFormat_Create();
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_WIDTH, w);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_HEIGHT, h);
    // 新增：明确指定解码器输出的像素格式 (NV12是鸿蒙AVC解码的通用格式)
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_SURFACE_FORMAT);
    // OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_NV12);

    if (!codecConfig.empty()) {
        OH_AVFormat_SetBuffer(fmt, OH_MD_KEY_CODEC_CONFIG,
                              const_cast<uint8_t*>(codecConfig.data()),
                              static_cast<int32_t>(codecConfig.size()));
        LOGI("Decoder: set codec_config in configure: %zu bytes", codecConfig.size());
    }

    if (OH_VideoDecoder_Configure(decoder_, fmt) != AV_ERR_OK) {
        LOGE("Decoder Configure failed");
        OH_AVFormat_Destroy(fmt);
        OH_VideoDecoder_Destroy(decoder_); decoder_ = nullptr; return false;
    }
    OH_AVFormat_Destroy(fmt);

    OH_AVErrCode err = OH_VideoDecoder_SetSurface(decoder_, outputSurface_);
    if (err != AV_ERR_OK) {
        LOGE("SetSurface err=%d", err);
        OH_VideoDecoder_Destroy(decoder_); decoder_ = nullptr; return false;
    }

    needKeyFrame_.store(true);
    stopped_.store(false);
    { std::lock_guard<std::mutex> m(matchMutex_); while (!inputQueue_.empty()) inputQueue_.pop(); }

    LOGI("Decoder init OK"); return true;
}

bool VideoDecoder::Start() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (running_.load() || !decoder_) return false;
    if (OH_VideoDecoder_Prepare(decoder_) != AV_ERR_OK) { LOGE("Prepare fail"); return false; }
    needKeyFrame_.store(true);
    if (OH_VideoDecoder_Start(decoder_) != AV_ERR_OK) { LOGE("Start fail"); return false; }
    running_.store(true);
    LOGI("Decoder started"); return true;
}

void VideoDecoder::Stop() {
    if (!running_.exchange(false)) return;
    stopped_.store(true);
    matchCond_.notify_all();
    std::lock_guard<std::mutex> lk(mutex_);
    if (decoder_) { OH_VideoDecoder_Stop(decoder_); OH_VideoDecoder_Reset(decoder_); }
    { std::lock_guard<std::mutex> m(matchMutex_); while (!inputQueue_.empty()) inputQueue_.pop(); }
    LOGI("Decoder stopped");
}

bool VideoDecoder::IsRunning() const { return running_.load(); }

bool VideoDecoder::QueuePacket(const uint8_t* data, size_t size, int64_t pts,
                                bool isKeyFrame, bool isCodecConfig) {
    if (!running_.load() || stopped_.load() || !data || size == 0) return false;

    if (!isCodecConfig) {
        if (needKeyFrame_.load()) {
            if (!isKeyFrame) return true;
            needKeyFrame_.store(false);
            LOGI("Decoder got first keyframe, sz=%zu", size);
        }
    }

    auto pkt = std::make_unique<InputPacket>();
    pkt->data.assign(data, data + size);
    pkt->pts = pts;
    pkt->isKeyFrame = isKeyFrame;
    pkt->isCodecConfig = isCodecConfig;

    { std::lock_guard<std::mutex> lk(matchMutex_); inputQueue_.push(std::move(pkt)); }
    matchCond_.notify_one();
    return true;
}

// ============================================================
// FillAndPush — 在回调内填充并提交
// ============================================================
void VideoDecoder::FillAndPush(OH_AVCodec* codec, uint32_t index,
                                OH_AVBuffer* buffer, const InputPacket& pkt) {
    int32_t cap = OH_AVBuffer_GetCapacity(buffer);
    uint8_t* dst = OH_AVBuffer_GetAddr(buffer);

    LOGI("FillAndPush: idx=%u cap=%d dst=%p sz=%zu cfg=%d key=%d",
         index, cap, dst, pkt.data.size(), pkt.isCodecConfig, pkt.isKeyFrame);

    if (!dst || cap <= 0) { LOGE("Buffer invalid!"); return; }
    if ((size_t)cap < pkt.data.size()) { LOGE("Buffer too small!"); return; }

    // 拷贝数据到 Buffer 中
    memcpy(dst, pkt.data.data(), pkt.data.size());

    // 配置 Buffer 属性
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
    
    // 将属性设置给当前的 OH_AVBuffer
    OH_AVBuffer_SetBufferAttr(buffer, &attr);

    // 修改处：必须使用 API 11+ 对应的 OH_VideoDecoder_PushInputBuffer，不再传递 attr 参数
    OH_AVErrCode ret = OH_VideoDecoder_PushInputBuffer(codec, index);
    
    LOGI("PushInputData: ret=%d idx=%u sz=%d flags=0x%x",
         ret, index, attr.size, attr.flags);
}

// ============================================================
// OnNeedInputBuffer — 回调内等待数据并提交
// ============================================================
void VideoDecoder::OnNeedInputBuffer(OH_AVCodec* codec, uint32_t index,
                                      OH_AVBuffer* buffer, void* userData) {
    auto* dec = static_cast<VideoDecoder*>(userData);
    if (!dec || dec->stopped_.load() || !buffer) return;

    LOGI("OnNeedInputBuffer: idx=%u", index);

    // 快速检查队列
    std::unique_ptr<InputPacket> pkt;
    {
        std::lock_guard<std::mutex> lk(dec->matchMutex_);
        if (!dec->inputQueue_.empty()) {
            pkt = std::move(dec->inputQueue_.front());
            dec->inputQueue_.pop();
        }
    }
    if (pkt) { dec->FillAndPush(codec, index, buffer, *pkt); return; }

    // 等待数据
    {
        std::unique_lock<std::mutex> lk(dec->matchMutex_);
        bool got = dec->matchCond_.wait_for(lk, std::chrono::milliseconds(500), [dec]{
            return !dec->inputQueue_.empty() || dec->stopped_.load();
        });
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

void VideoDecoder::OnNewOutputBuffer(OH_AVCodec* codec, uint32_t index,
                                      OH_AVBuffer* buffer, void* userData) {
    auto* dec = static_cast<VideoDecoder*>(userData);
    if (!dec || dec->stopped_.load()) return;

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
    if (decoder_) { OH_VideoDecoder_Destroy(decoder_); decoder_ = nullptr; outputSurface_ = nullptr; }
}