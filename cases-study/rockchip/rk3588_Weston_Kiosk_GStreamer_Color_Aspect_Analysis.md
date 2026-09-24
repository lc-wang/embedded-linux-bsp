# RK3588 Weston Kiosk Mode 顯示異常分析報告

## 1. 問題概述（背景與問題描述）

在嵌入式系統（RK3588 ）上使用 **Weston + GStreamer** 播放影片時，觀察到以下差異行為：

-   **Desktop / 一般 Weston terminal 模式**：
    
    -   影片顏色正常
        
    -   顯示比例正確
        
-   **Weston kiosk mode**：
    
    -   初期現象：畫面顏色明顯偏暗
        
    -   修正顏色後：顯示比例被拉伸（aspect ratio 不正確）

## 2. 系統環境

-   SoC：Rockchip RK3588
    
-   Display Server：Weston (kiosk mode / desktop mode)
    
-   Multimedia Framework：GStreamer 1.x
    
-   Video Sink：waylandsink
    
-   測試影片：H.264 MP4 (BT.709, limited range)
    
## 3. 除錯過程

### 3.1 問題一：Kiosk mode 下畫面顏色偏暗

#### 3.1.1 現象

在 kiosk mode 下執行以下 pipeline：

```bash
gst-launch-1.0 filesrc location=./480p_demo.mp4 ! \
  decodebin ! videoconvert ! videoscale ! \
  video/x-raw,width=480,height=360 ! \
  waylandsink
```

顯示結果：

-   影片可正常播放
    
-   但整體亮度偏低、畫面發暗
    
### 3.2 問題二：嘗試 RGB / full-range 導致黑畫面

#### 3.2.1 嘗試的 pipeline

```bash
gst-launch-1.0 filesrc location=./480p_demo.mp4 ! \
  decodebin ! videoconvert ! videoscale ! \
  video/x-raw,format=RGB,colorimetry=bt709,range=full,width=480,height=360 ! \
  waylandsink

```

#### 3.2.2 結果

-   畫面幾乎全黑
    
-   顯示比例異常
    
### 3.3 問題三：顯示比例（Aspect Ratio）不正確

#### 3.3.1 現象

在顏色修正後：

-   影片畫面被拉伸填滿整個螢幕
    
-   原始比例未被保留
    
## 4. Root Cause 分析

### 4.1 問題一：成因分析

-   大多數 MP4 / H.264 影片為 **BT.709 + limited range (16–235)**
    
-   在 **desktop mode**：
    
    -   Weston compositor 會進行 scene 合成與 gamma / color correction
        
    -   limited-range 影像可被正確顯示
        
-   在 **kiosk mode**：
    
    -   waylandsink 優先使用 **DRM overlay plane**
        
    -   影片 buffer 可能直接 scanout
        
    -   **不保證套用 gamma / color correction**
        
結果為：

> limited-range YUV buffer 被直接輸出，視覺上呈現為整體偏暗。

### 4.2 問題二：原因說明

-   在 kiosk mode 下，waylandsink 多半走 **DRM overlay plane**
    
-   RK3588 overlay plane 通常：
    
    -   ✗ 不支援 RGB buffer
        
    -   ✗ 不完整支援 color range metadata
        
-   強制指定 RGB + full-range 可能導致：
    
    -   buffer negotiation 失敗
        
    -   plane fallback 行為不完整
        
此行為在 desktop mode 下可能被 compositor 掩蓋，但在 kiosk mode 會直接暴露。

### 4.3 問題三：原因分析

-   Weston kiosk mode 的設計行為：
    
    -   surface 預設 fullscreen
        
    -   overlay plane 只負責將 buffer 拉伸至輸出大小
        
    -   **不提供 aspect-ratio preservation 或 letterbox 機制**
        
-   Desktop mode 則由 compositor 負責比例處理，因此行為不同。
    
## 5. 解決方案

### 5.1 顏色修正的正確做法

#### 5.1.1 建議 pipeline（顏色正確）

```bash
gst-launch-1.0 filesrc location=./480p_demo.mp4 ! \
  decodebin ! videoconvert ! videoscale ! \
  video/x-raw,format=NV12,colorimetry=bt709 ! \
  waylandsink

```

說明：

-   維持 YUV (NV12)
    
-   不強制指定 range
    
-   符合 DRM overlay plane 的實際支援能力
    
結果：

-   kiosk mode 下顏色顯示正常
    
### 5.2 問題三：正確解法

#### 5.2.1 使用 videobox 預先處理比例

```bash
gst-launch-1.0 filesrc location=./480p_demo.mp4 ! \
  decodebin ! videoconvert ! videoscale ! \
  videobox top=<T> bottom=<B> left=<L> right=<R> ! \
  video/x-raw,format=NV12,width=<panel_w>,height=<panel_h> ! \
  waylandsink

```

說明：

-   在 application 層完成 scaling 與 letterboxing
    
-   DRM plane 僅負責顯示「最終畫面」
    
-   不依賴 Weston 或 driver 的比例處理能力
   
