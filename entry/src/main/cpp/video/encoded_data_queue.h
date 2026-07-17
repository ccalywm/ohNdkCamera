#pragma once


#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <atomic>
#include <cstdint>

// ============================================================
// 编码数据队列，用于线程安全地传递编码后的视频帧数据包
// ============================================================
class EncodedDataQueue {
public:
    struct Packet {
        std::vector<uint8_t> data;  // 编码数据（不含起始码，或含起始码视情况）
        int64_t pts = 0;            // 时间戳（微秒或纳秒，与编码器约定）
        bool isKeyFrame = false;    // 是否为关键帧（IDR）
        bool isCodecConfig = false; // 是否为码流配置信息（SPS/PPS）
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
 