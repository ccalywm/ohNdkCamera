# 视频处理流程（OpenHarmony）

## 整体架构

```text
                Camera
                   │
                   ▼
          Camera Output Surface
                   │
                   ▼
            OpenGL Renderer
                   │
         ┌─────────┴─────────┐
         │                   │
         ▼                   ▼
 Preview Surface       Video Encoder Surface
（本地预览）                 │
                             ▼
                    H.264 Encoded Stream
                             │
                     Thread Safe Queue
                             │
                             ▼
                      Video Decoder
                             │
                             ▼
                     Display Surface
```

---

# 流程说明

## 1. 摄像头采集

摄像头负责采集实时视频数据。

输出目标：

- OpenGL 输入 Surface

摄像头采集的数据直接交给 OpenGL，不经过 CPU 处理。

---

## 2. OpenGL 渲染

OpenGL 是整个视频链路的核心模块。

主要负责：

- 接收摄像头纹理
- 图像旋转
- 镜像
- 缩放
- 水印（可扩展）
- 美颜（可扩展）
- Shader 特效（可扩展）

完成一次渲染后，同时输出到两个目标。

### 输出一：本地预览

输出到：

```
Preview Surface
```

用于：

- 页面实时预览
- 用户查看摄像头画面

---

### 输出二：视频编码器

输出到：

```
Video Encoder Surface
```

编码器直接从 Surface 获取图像进行硬件编码，无需 CPU 拷贝。

编码格式：

```
H.264
```

---

# 3. 视频编码

编码器负责将 GPU 图像编码为 H.264 数据。

数据流：

```text
OpenGL
    │
    ▼
Video Encoder
    │
    ▼
H.264
```

输出：

```
Encoded Packet
```

每个数据包可能包含：

- SPS
- PPS
- IDR Frame
- P Frame

---

# 4. 数据共享

编码线程与解码线程之间通过线程安全队列通信。

```text
Encoder Thread
        │
        ▼
Thread Safe Queue
        │
        ▼
Decoder Thread
```

作用：

- 解耦编码与解码
- 缓冲数据
- 避免线程阻塞

数据结构示例：

```cpp
struct VideoPacket
{
    uint8_t* data;
    uint32_t size;
    int64_t pts;
    bool keyFrame;
};
```

---

# 5. 视频解码

解码线程不断读取编码后的 H.264 数据。

```text
Queue
   │
   ▼
Video Decoder
```

解码后直接输出到 Surface。

整个过程采用硬件解码。

---

# 6. 视频显示

解码器输出：

```
Display Surface
```

用于：

- 视频播放
- 本地回显
- 远端视频显示

数据流：

```text
Video Decoder
      │
      ▼
Display Surface
      │
      ▼
屏幕显示
```

---

# 数据流

```text
Camera
    │
    ▼
OpenGL Renderer
    ├──────────────► Preview Surface
    │
    ▼
Video Encoder Surface
    │
    ▼
Video Encoder
    │
    ▼
H.264 Stream
    │
    ▼
Thread Safe Queue
    │
    ▼
Video Decoder
    │
    ▼
Display Surface
```

---

# 模块职责

| 模块 | 职责 |
|------|------|
| Camera | 摄像头采集 |
| OpenGL Renderer | 图像渲染、图像处理、多路输出 |
| Preview Surface | 本地预览 |
| Video Encoder | H.264 硬件编码 |
| Thread Safe Queue | 编码与解码线程通信 |
| Video Decoder | H.264 硬件解码 |
| Display Surface | 视频显示 |

---

# 线程划分

```text
Camera Thread
      │
      ▼
OpenGL Thread
      │
      ├────────► Preview Surface
      │
      ▼
Encoder Thread
      │
      ▼
Thread Safe Queue
      │
      ▼
Decoder Thread
      │
      ▼
Display Surface
```

---

# 特点

- Camera → OpenGL 全 GPU 渲染流程。
- OpenGL 一次渲染，同时输出到预览和编码器。
- 编码器通过 Surface 获取图像，减少 CPU 拷贝。
- 编码线程与解码线程通过线程安全队列解耦。
- 解码器直接输出到 Surface，实现硬件加速显示。
- 整个架构模块清晰，便于后续扩展网络传输、录像、截图、水印、美颜等功能。