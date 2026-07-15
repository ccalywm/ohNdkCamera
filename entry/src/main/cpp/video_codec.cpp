#include "video_codec.h"
#include <chrono>
#include <cstring>

#include <cstdio>
#include <fstream>
#include <sys/stat.h>
#include <unistd.h>

static FILE* g_encoderDump = nullptr;
static std::mutex g_dumpMutex;
// 从 H.264 Annex B 码流中提取 SPS (nal_type=7) 和 PPS (nal_type=8) 数据
// 返回拼接后的 std::vector<uint8_t>（包含起始码 00 00 00 01 + NAL）
static std::vector<uint8_t> ExtractSPSPPSFromAnnexB(const uint8_t* data, size_t size) {
    std::vector<uint8_t> result;
    size_t pos = 0;
    while (pos + 4 < size) {
        // 查找起始码 (00 00 00 01 或 00 00 01)
        size_t startCodeLen = 0;
        if (pos + 4 <= size && data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) {
            startCodeLen = 4;
        } else if (pos + 3 <= size && data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) {
            startCodeLen = 3;
        } else {
            pos++;
            continue;
        }
        size_t start = pos + startCodeLen; // NAL 起始位置
        if (start >= size) break;
        uint8_t nalType = data[start] & 0x1F;
        // 查找下一个起始码，确定当前 NAL 结束位置
        size_t end = size;
        for (size_t j = start + 1; j + 3 < size; ++j) {
            if ((data[j] == 0 && data[j+1] == 0 && data[j+2] == 0 && data[j+3] == 1) ||
                (data[j] == 0 && data[j+1] == 0 && data[j+2] == 1)) {
                end = j;
                break;
            }
        }
        size_t nalLen = end - start;
        if (nalType == 7 || nalType == 8) { // SPS 或 PPS
            // 添加起始码
            result.insert(result.end(), {0,0,0,1});
            // 添加 NAL 数据
            result.insert(result.end(), data + start, data + start + nalLen);
        }
        pos = end;
    }
    return result;
}
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
    
    // 【关键修改】：Surface输入模式下，必须设置为 AV_PIXEL_FORMAT_SURFACE_FORMAT
    // 设置为 NV12 会导致底层无法正确转换 OpenGL 写入的 RGBA 缓冲区，从而引发粉黄花屏
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_RGBA);
    // OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_PIXEL_FORMAT, AV_PIXEL_FORMAT_NV12);
    
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_BITRATE, kbps );
    // OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_FRAME_RATE, fps);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_FRAME_RATE, fps);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_I_FRAME_INTERVAL, 1000);
    
    // OH_AVFormat_SetLongValue(fmt, OH_MD_KEY_BITRATE, 500000);
    // OH_AVFormat_SetLongValue(fmt, OH_MD_KEY_BITRATE, 790000);
    // OH_AVFormat_SetLongValue(fmt, OH_MD_KEY_BITRATE, 2000000);
    // OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, OH_BitrateMode::BITRATE_MODE_VBR);
    OH_AVFormat_SetIntValue(fmt, OH_MD_KEY_VIDEO_ENCODE_BITRATE_MODE, OH_BitrateMode::BITRATE_MODE_CBR);
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

    LOGI("Enc out: sz=%d pts=%lld key=%d cfg=%d flags=0x%x [%02x %02x %02x %02x %02x]",
         attr.size, (long long)pts, isKey, isConfig, attr.flags,
         data[0], data[1], data[2], data[3], data[4]);

    // 如果是关键帧且尚未发送 codec_config，提取 SPS/PPS
    if (isKey && !enc->hasSentCodecConfig_) {
        std::lock_guard<std::mutex> cl(enc->codecMutex_);
        if (enc->codecConfig_.empty()) {
            // 1. 先尝试从输出格式获取（如果之前未获取到）
            OH_AVFormat* outFmt = OH_VideoEncoder_GetOutputDescription(codec);
            if (outFmt) {
                uint8_t* cfg = nullptr;
                size_t cfgSz = 0;
                if (OH_AVFormat_GetBuffer(outFmt, "codec_config", &cfg, &cfgSz) && cfg && cfgSz > 0) {
                    enc->codecConfig_.assign(cfg, cfg + cfgSz);
                    LOGI("Got codec_config from format: %zu bytes", cfgSz);
                }
                OH_AVFormat_Destroy(outFmt);
            }

            // 2. 如果格式中没有，则从当前帧数据中提取 SPS/PPS
            if (enc->codecConfig_.empty()) {
                enc->codecConfig_ = ExtractSPSPPSFromAnnexB(data, attr.size);
                if (!enc->codecConfig_.empty()) {
                    LOGI("Extracted SPS/PPS from keyframe: %zu bytes", enc->codecConfig_.size());
                } else {
                    LOGE("Failed to extract SPS/PPS from keyframe!");
                }
            }
        }

        // 如果成功获取到 codec_config，推送一个配置包
        if (!enc->codecConfig_.empty()) {
            auto cpkt = std::make_unique<EncodedDataQueue::Packet>();
            cpkt->data = enc->codecConfig_;
            cpkt->pts = 0;
            cpkt->isKeyFrame = false;
            cpkt->isCodecConfig = true;
            enc->outputQueue_->Push(std::move(cpkt));
            LOGI("Pushed codec_config: %zu bytes", cpkt->data.size());
            enc->hasSentCodecConfig_ = true;
        }
    }

    // ---------- 调试：保存编码数据到文件 ----------

    // {
    //     std::lock_guard<std::mutex> lock(g_dumpMutex);
    //     if (!g_encoderDump) {
    //         // 尝试打开文件（路径可改为应用缓存目录，例如 /data/data/com.your.app/cache/encoder.h264）
    //         const char* path = "/data/storage/el2/base/haps/entry/cache/encoder1.h264";  // 需要 adb shell 权限
    //         // 或者使用应用私有目录: /data/data/<package>/cache/encoder.h264
    //         g_encoderDump = fopen(path, "wb");
    //         if (g_encoderDump) {
    //             LOGI("Opened file encoder dump file: %s", path);
    //         } else {
    //             LOGE("Opened file Failed to open dump file: %s", path);
    //         }
    //     }
    //     if (g_encoderDump) {
    //         fwrite(data, 1, attr.size, g_encoderDump);
    //         fflush(g_encoderDump);
    //         LOGI("Opened file %d bytes to encoder.h264", attr.size);
    //     }
    // }
// ---------- 调试结束 ----------
    // 推送常规视频帧
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
    
  
    

    // 在 VideoDecoder::Init 中，配置 format 之前：
    std::vector<uint8_t> finalConfig;
    if (!codecConfig.empty()) {
        finalConfig = codecConfig;
    } else {
        // 硬编码 SPS/PPS (640x480 Baseline)
        // static const uint8_t kSPS[] = {0x00,0x00,0x00,0x01,0x67,0x42,0x80,0x1e,0x8a,0x80,0x04,0x54,0xde,0x3c,0x80};
        // static const uint8_t kPPS[] = {0x00,0x00,0x00,0x01,0x68,0x42,0x80};
        // finalConfig.insert(finalConfig.end(), kSPS, kSPS+sizeof(kSPS));
        // finalConfig.insert(finalConfig.end(), kPPS, kPPS+sizeof(kPPS));
        // LOGI("Using hardcoded SPS/PPS, size=%zu", finalConfig.size());
    }
    
    // 然后设置到 format:
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