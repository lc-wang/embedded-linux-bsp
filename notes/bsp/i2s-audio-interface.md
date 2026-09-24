# BSP I2S / Audio Interface（音訊介面整合實務）

> 本章定位：
>
> -   站在 **BSP / SoC Bring-up Engineer** 視角，理解 I2S / Audio 介面為什麼特別容易出問題
>
> -   不講 I2S waveform 或 codec datasheet，而是聚焦 **clock、PLL、同步關係與系統整合**
>
> -   能實際用於 debug：沒聲音、爆音、聲音變慢/變快、suspend/resume 後失效
>

## 1. 為什麼 Audio 是 BSP 最容易「怪怪的」介面

Audio 在 BSP 世界有幾個致命特性：

-   對 **clock 精準度與穩定度極度敏感**
-   問題常不是「完全不能用」，而是「聽起來不對」
-   很多錯誤只在 runtime 或 suspend/resume 後出現

**Audio 問題通常不是 codec driver bug，而是 clock / 同步關係錯誤。**

## 2. I2S Interface 的核心不是資料，而是 Clock

### 2.1 Audio Clock 為什麼比資料重要

I2S 傳輸包含：

-   BCLK（bit clock）
-   LRCLK / WS（frame clock）
-   MCLK（master clock，若有）

資料本身很少出錯，
真正決定聲音品質的是：

-   clock source
-   clock rate
-   clock 穩定度

### 2.2 Clock Master / Slave 關係

在 Audio 系統中，必須明確一件事：

> **誰產生 clock？**

常見配置：

-   SoC 為 master，codec 為 slave
-   Codec 為 master，SoC 為 slave

如果角色搞錯：

-   聲音可能能播
-   但會有 pitch error、drop、爆音

## 3. I2S Controller 與 Audio Codec 的分工

### 3.1 I2S Controller（SoC 端）

-   提供 clock
-   控制資料格式（I2S / left-justified / DSP mode）
-   通常高度依賴 PLL

### 3.2 Audio Codec（外掛裝置）

-   依賴正確 clock 才能鎖定
-   需要特定 MCLK / BCLK 比例

**Codec 沒 lock clock 時，通常不會明確報錯。**

## 4. Device Tree 中 Audio 最容易錯的地方

### 4.1 clock 與 PLL 設定

-   clock source 選錯
-   PLL rate 不符合 audio family（44.1k / 48k）

結果：

-   聲音速度錯
-   pitch 不對

### 4.2 DAI link 關係

-   CPU DAI / Codec DAI 關係錯誤
-   format / clock inversion 不一致

### 4.3 pinmux 與電氣問題

-   I2S 腳位未正確 mux
-   線長或 EMI 影響 clock

## 5. 為什麼 Audio 常在 Suspend / Resume 後壞掉

常見原因：
-   PLL 在 suspend 被關閉
-   resume 未重新鎖定 clock
-   codec 未重新初始化

結果：
-   播放成功但沒聲音
-   聲音斷斷續續

**這是 BSP Audio 的經典問題。**

## 6. 為什麼「有聲音」不代表設定正確

Audio 問題常見誤判：
-   只要能播，就覺得 OK
-   忽略 clock 精準度

實際上：
-   clock 微小誤差
-   長時間播放會累積成明顯問題

## 7. Audio Debug Toolbox

### 7.1 確認音效卡與 DAI 是否存在

```bash
aplay -l
arecord -l
```
觀察重點：
-   是否看到預期的 sound card    
-   card / device 編號是否穩定
    
若這一步就不存在：
-   問題通常在 **DAI link / DTS / driver probe**
    
### 7.2 確認 Mixer / Path 是否正確

```bash
amixer
```
或在嵌入式系統常見：
```bash
tinymix
tinymix controls
tinymix <id> <value>
```
用途：
-   確認 codec path 是否打開
-   排除「有資料但被 mute」的情況

### 7.3 固定條件播放測試

```bash
aplay -D hw:0,0 test.wav
```
建議使用：
-   單一 sample rate  
-   單一 channel

若此情況仍異常：
-   問題多半在 **clock / PLL / DAI format**

### 7.4 Clock / PLL 狀態檢查

```bash
cat /sys/kernel/debug/clk/clk_summary
```
觀察重點：
-   Audio 相關 clock 是否 enable 
-   rate 是否符合 44.1k / 48k family
    
Audio 問題第一時間一定要看這裡。

### 7.5 Suspend / Resume Audio 檢查流程

```bash
aplay test.wav
echo mem > /sys/power/state
aplay test.wav
```
若 resume 後：
-   沒聲音 / 雜音 / 只播一下

通常代表：
-   PLL 未重新 lock
-   codec 未重新初始化

### 7.6 快速問題定位表

| 現象               | 最可能問題層級        |
|--------------------|-----------------------|
| 看不到 Sound Card  | DAI / DTS             |
| 有 Card 無聲音     | Mixer / Path          |
| 聲音快慢不對       | Clock rate 設定       |
| 爆音 / 雜音        | PLL 不穩              |
| Resume 後壞掉      | Clock / Codec reset   |

## 8. 常見問題與排查

### 8.1 Debug Checklist（實戰導向）

#### Clock

-   確認 MCLK / BCLK / LRCLK 是否存在
-   確認 rate 是否符合 codec datasheet

#### DTS / DAI

-   檢查 DAI format
-   檢查 master/slave 設定

#### Runtime 行為

-   長時間播放是否穩定
-   suspend/resume 後是否仍正常

### 8.2 常見錯誤歸因

| 現象     | 常見誤判        | 真正原因          |
|----------|-----------------|-------------------|
| 沒聲音   | Codec driver    | Clock 未 lock     |
| 爆音     | Buffer 問題     | PLL 不穩          |
| 聲音快慢 | App 問題        | Rate mismatch     |
