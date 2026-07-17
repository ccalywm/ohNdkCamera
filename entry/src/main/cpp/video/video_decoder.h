#pragma once


#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <atomic>
#include <cstdint>
#include <string>

#include <multimedia/player_framework/native_avcodec_videodecoder.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avformat.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include <native_window/external_window.h>

// ============================================================
// 视频解码器类
// 接收编码数据包，解码后渲染到指定 Surface
// ============================================================
class VideoDecoder {
public:
    VideoDecoder();
    ~VideoDecoder();

    // 初始化解码器，可传入 codecConfig（SPS/PPS）以支持解析
    bool Init(OHNativeWindow* outputSurface, const std::string& mime = "video/avc",
              int32_t width = 640, int32_t height = 480,
              const std::vector<uint8_t>& codecConfig = {});

    bool Start();
    void Stop();
    bool IsRunning() const;

    // 将编码数据包送入解码器队列
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
