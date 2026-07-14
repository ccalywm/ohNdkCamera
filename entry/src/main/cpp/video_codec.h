#ifndef VIDEO_CODEC_H
#define VIDEO_CODEC_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <atomic>
#include <cstdint>

#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avformat.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include <native_window/external_window.h>

#include "util/DebugLog.h"

// ============================================================
class EncodedDataQueue {
public:
    struct Packet {
        std::vector<uint8_t> data;
        int64_t pts = 0;
        bool isKeyFrame = false;
        bool isCodecConfig = false;
    };
    void Push(std::unique_ptr<Packet> pkt);
    std::unique_ptr<Packet> Pop(int timeoutMs = 100);
    void Stop();
    size_t Size() const;
private:
    std::queue<std::unique_ptr<Packet>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<bool> stopped_{false};
};

// ============================================================
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();
    bool Init(int32_t w, int32_t h, int32_t kbps, int32_t fps,
              const std::string& mime = "video/avc");
    bool Start();
    void Stop();
    OHNativeWindow* GetInputSurface() const;
    std::shared_ptr<EncodedDataQueue> GetOutputQueue() const;
    bool IsRunning() const;
    // 新增：获取编码器的 codec_config (SPS/PPS)
    std::vector<uint8_t> GetCodecConfig() const;
private:
    static void OnError(OH_AVCodec*, int32_t, void*);
    static void OnStreamChanged(OH_AVCodec*, OH_AVFormat*, void*);
    static void OnNeedInputBuffer(OH_AVCodec*, uint32_t, OH_AVBuffer*, void*);
    static void OnNewOutputBuffer(OH_AVCodec*, uint32_t, OH_AVBuffer*, void*);
    void Cleanup();

    OH_AVCodec* encoder_ = nullptr;
    OHNativeWindow* inputSurface_ = nullptr;
    std::shared_ptr<EncodedDataQueue> outputQueue_;
    std::atomic<bool> running_{false};
    std::mutex mutex_;
    std::atomic<int64_t> ptsCounter_{0};
    int32_t width_ = 0;
    int32_t height_ = 0;
    bool hasSentCodecConfig_ = false;
    mutable std::mutex codecMutex_;
    std::vector<uint8_t> codecConfig_;
};

// ============================================================
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();
    bool Init(OHNativeWindow* outputSurface, const std::string& mime = "video/avc",
              int32_t width = 640, int32_t height = 480,
              const std::vector<uint8_t>& codecConfig = {});
    bool Start();
    void Stop();
    bool IsRunning() const;
    bool QueuePacket(const uint8_t* data, size_t size, int64_t pts,
                     bool isKeyFrame, bool isCodecConfig = false);
private:
    struct InputPacket {
        std::vector<uint8_t> data;
        int64_t pts = 0;
        bool isKeyFrame = false;
        bool isCodecConfig = false;
    };
    void FillAndPush(OH_AVCodec* codec, uint32_t index,
                     OH_AVBuffer* buffer, const InputPacket& pkt);
    static void OnError(OH_AVCodec*, int32_t, void*);
    static void OnStreamChanged(OH_AVCodec*, OH_AVFormat*, void*);
    static void OnNeedInputBuffer(OH_AVCodec*, uint32_t, OH_AVBuffer*, void*);
    static void OnNewOutputBuffer(OH_AVCodec*, uint32_t, OH_AVBuffer*, void*);
    void Cleanup();

    OH_AVCodec* decoder_ = nullptr;
    OHNativeWindow* outputSurface_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<bool> needKeyFrame_{true};
    std::atomic<bool> stopped_{false};
    std::mutex mutex_;

    std::queue<std::unique_ptr<InputPacket>> inputQueue_;
    std::mutex matchMutex_;
    std::condition_variable matchCond_;
};

#endif // VIDEO_CODEC_H