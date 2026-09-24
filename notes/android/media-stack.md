# Android Media Stack 全解析（Audio / Video / Camera / Codec）

## 1. Android Media 架構總覽

```text
App
↓
Framework API (MediaCodec / Camera2 / AudioTrack)
↓
MediaServer / Stagefright
↓
Codec / Camera / Audio HAL (HIDL/AIDL)
↓
硬體後端：VPU / DSP / ISP / ALSA
↓
輸出（Speaker / Display / Network）
```

涉及的進程：

| 進程 | 角色 |
| --- | --- |
| `mediaserver` | 本體：MediaCodec、MediaExtractor、CameraService |
| `cameraserver` | Camera HAL 2/3 |
| `audioserver` | AudioFlinger + HAL |
| `media.codec` | MediaCodecService |
| `media.extractor` | ExtractorService |

## 2. MediaCodec（硬體編碼 / 解碼）

### 2.1 MediaCodec Pipeline

```text
App
↓ MediaCodec API
Binder
↓
MediaCodecService (mediaserver)
↓
OMX / Codec2 (C2 HAL)
↓
VPU / DSP / GPU
```

Android 10 之後使用 **Codec2.0 (C2)** 架構取代 OMX。

#### 重要類別

| 類別 | 角色 |
| --- | --- |
| `MediaCodec` | App-side API |
| `MediaCodecService` | Service，管理 codec 進程 |
| `CCodec` | Codec2 軟體代理 |
| `IComponentStore` | HAL 進入點 |
| `GraphicBuffer` | buffer 傳遞（使用 gralloc HAL） |

### 2.2 Buffer Flow（解碼）

```text
Input bitstream → queueInputBuffer
↓
Codec 解碼
↓
Output GraphicBuffer（SurfaceTexture)
↓
SurfaceFlinger 合成顯示
```

與顯示架構（前章）密切整合。

#### 相關 HAL

| HAL | 說明 |
| --- | --- |
| Codec2 HAL | 控制 VPU/DSP 編解碼 |
| gralloc HAL | 分配 GraphicBuffer |
| HWC HAL | 最終顯示 |

## 3. Camera HAL 3 Pipeline

### 3.1 Camera 整體流程（HAL v3）

```text
App (Camera2 API)
↓
CameraManager
↓
CameraService (cameraserver)
↓
Camera HALv3 (HIDL or AIDL)
↓
ISP / Sensor / MIPI-CSI / DMA
↓
Output Stream
```

#### HAL v3 特點

| 層級 | 功能 |
| --- | --- |
| Request | App → HAL |
| Pipeline | HAL pipeline 處理 |
| Result | HAL → App |

#### Camera Buffer 方向

| 類型 | 流向 |
| --- | --- |
| Preview | HAL → SurfaceFlinger |
| Video | HAL → MediaCodec（編碼） |
| Still | HAL → App / JPEG encode |

### 3.2 三大物件：Request / Stream / Metadata

| 物件 | 說明 |
| --- | --- |
| `CaptureRequest` | 指令：ISO、曝光、輸出格式 |
| `CaptureResult` | 回報：metadata + buffer |
| Streams | 儲存 / 顯示 / 編碼用 buffer |

## 4. Audio Stack（AudioFlinger / AAudio / HAL）

### 4.1 Audio 系統架構

```text
App
↓ (AudioTrack / AudioRecord / AAudio)
AudioManager
↓
AudioFlinger (audioserver)
↓
Audio HAL (HIDL/AIDL)
↓
ALSA / DSP / Mixer
↓
Speaker / Mic
```

#### AudioFlinger 職責

| 模組 | 功能 |
| --- | --- |
| MixerThread | 多路 audio stream 混音 |
| PlaybackThread | 控制播放 |
| RecordThread | 控制錄音 |
| EffectModule | EQ、回音消除、壓縮 |
| AudioPolicy | 音訊路由 earpiece / speaker / BT |

### 4.2 AAudio（低延遲音訊）

Android 8+ 引入 **AAudio** 用於低延遲場景：

| 功能 | 說明 |
| --- | --- |
| OpenSL ES 替代方案 | 更低 latency |
| 常用於遊戲引擎 | Unity / Unreal |
| 支援 MMAP | 減少複製降低延遲 |

## 5. Media Extractor / Renderer

### 5.1 MediaServer 管線（音樂與影片播放）

```text
MediaPlayer
↓
MediaExtractor → 解封裝（MP4/MKV/TS）
↓
MediaCodec → 解碼
↓
AudioTrack / Surface → 輸出
```

Extractor Service（Android 10+）拆成獨立進程：

| 進程 | 功能 |
| --- | --- |
| `media.extractor` | 解析封裝格式 |
| `media.codec` | 編解碼 |
| `mediaserver` | 播放器、後端 |

## 6. Buffer 與 Graphic 路徑（Video Rendering）

### 6.1 Video Rendering

```text
解碼後 GraphicBuffer
↓
SurfaceTexture
↓
BufferQueue
↓
SurfaceFlinger
↓
HWC → DRM → Panel
```

與顯示架構緊密合作。

## 7. 支援硬體（VPU / DSP / Codec / ISP）

### 7.1 常見硬體加速單元

| 硬體 | 功能 |
| --- | --- |
| VPU | 硬體編解碼 |
| DSP | 音訊處理、語音加速 |
| ISP | Camera pipeline 處理 |
| GPU | 影像後處理 / 特效 |

### 7.2 HAL 調用點（Codec + Audio + Camera）

| HAL | 功能 |
| --- | --- |
| Codec2 HAL | 控制 VPU 解碼/編碼 |
| Camera HAL3 | 控制 sensor/ISP |
| Audio HAL | 控制 PCM、Mixer、Routing |
| gralloc HAL | 配置 GraphicBuffer |
| HWC HAL | 顯示最終輸出 |

## 8. Debug 工具與技巧

### 8.1 常用 Debug 方法

| 工具 | 用途 |
| --- | --- |
| `logcat -b media` | MediaServer log |
| `atrace` media, camera, audio | Pipeline 時序 |
| `dumpsys media.codec` | Codec 佇列狀態 |
| `dumpsys media.camera` | Camera 狀態 |
| `dumpsys media.audio_flinger` | Mixer 狀態 |
| `adb shell getevent` | Camera shutter/input 分析 |
| `mediaplayer --profile` | Media 性能 |
