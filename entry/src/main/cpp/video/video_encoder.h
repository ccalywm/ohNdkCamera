#pragma once

#include <memory>
#include <atomic>
#include <mutex>
#include <vector>
#include <string>

#include <multimedia/player_framework/native_avcodec_videoencoder.h>
#include <multimedia/player_framework/native_avcodec_base.h>
#include <multimedia/player_framework/native_avformat.h>
#include <multimedia/player_framework/native_avbuffer.h>
#include <native_window/external_window.h>

#include "encoded_data_queue.h"

// ============================================================
// 视频编码器类
// 使用 Surface 输入原始帧，输出编码码流（通过 EncodedDataQueue 获取）
// ============================================================
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder();

    // 初始化编码器
    bool Init(int32_t w, int32_t h, int32_t kbps, int32_t fps,
              const std::string& mime = "video/avc");

    bool Start();
    void Stop();

    // 获取输入 Surface，供外部渲染写入原始帧
    OHNativeWindow* GetInputSurface() const;

    // 获取输出队列，用于接收编码后的数据包
    std::shared_ptr<EncodedDataQueue> GetOutputQueue() const;

    bool IsRunning() const;

    // 获取编码器的 codec_config (SPS/PPS)，可能为空
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

