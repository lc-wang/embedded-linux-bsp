# DAPM Routing 深度解析

> 為什麼 PCM 正常卻沒有聲音？

## 1. DAPM 是什麼？

DAPM = **Dynamic Audio Power Management**

它解決的問題是：

> 嵌入式裝置不能讓 DAC / ADC / Mixer 一直上電。

手機 / 平板 / SoC：

-   speaker

-   headset

-   mic

-   line-in

-   bluetooth

這些都是 **動態 power 控制的**。

## 2. ALSA Core 為什麼沒有 DAPM？

因為 PC sound card：

-   電源固定

-   routing 固定

-   mixer 直接控制

SoC world：

-   codec 裡面有 power island

-   每個 block 都可關閉

-   需要 graph-based power control

## 3. DAPM 的核心概念

DAPM 是一個：

> Graph-based power dependency engine

它會建立：

`[Input] → [Mixer] → [DAC] → [Output]` 

當你播放聲音時：

-   只打開必要的路徑

-   其他全部關閉

## 4. DAPM 的核心資料結構

```
struct snd_soc_dapm_widget
struct snd_soc_dapm_route
```

### 4.1 Widget 是什麼？

Widget 是「音訊 block」。

例如：

-   DAC

-   ADC

-   Mixer

-   PGA

-   Output

-   Input

-   Supply

範例：
```
SND_SOC_DAPM_DAC("DAC", "Playback", REG, BIT, 0),
SND_SOC_DAPM_OUTPUT("SPK"),
```

### 4.2 Route 是什麼？

Route 定義：

`Source → Destination` 
```
static  const  struct  snd_soc_dapm_route  routes[] = {
    {"SPK", NULL, "DAC"},
};
```
意思是：

`DAC → SPK` 

## 5. DAPM Graph 如何運作？

當你播放：

`aplay` 

流程：
```
PCM start
  ↓
ASoC 啟動 DAPM walk
  ↓
從 active endpoint 反推 graph
  ↓
打開必要 power
```
DAPM 會：

-   計算 graph

-   找到 source

-   只 enable 該路徑

## 6. 真實 codec 範例（以 WM8960 為例）

在 wm8960 driver 中：
```
static const struct snd_soc_dapm_widget wm8960_dapm_widgets[] = {
    SND_SOC_DAPM_DAC("Left DAC", "Playback", WM8960_POWER1, 8, 0),
    SND_SOC_DAPM_DAC("Right DAC", "Playback", WM8960_POWER1, 7, 0),
    SND_SOC_DAPM_OUTPUT("LOUT1"),
};
```
Route：

`{"LOUT1", NULL, "Left DAC"},` 

這定義：

`PCM → DAC → LOUT1` 

## 7. 為什麼會 PCM 正常但沒聲音？

這是 BSP 常見地獄。

情境：

-   aplay 正常

-   DMA 有跑

-   I2S 有 clock

-   codec register 正確

但 speaker 沒聲音。

原因通常是：

> DAPM route 沒接完整。

例如少了：

`{"SPK", NULL, "Mixer"},` 

或少了 supply：

`{"DAC", NULL, "VREF"},` 

## 8. debugfs 是你最好的朋友

掛載：

`mount -t debugfs none /sys/kernel/debug` 

查看：

`cat /sys/kernel/debug/asoc/dapm` 

可以看到：

-   哪些 widget 是 ON

-   哪些是 OFF

這是 debug 關鍵。

## 9. DAPM 的四種 Widget 類型

| 類型     | 說明                          |
|----------|-------------------------------|
| Endpoint | 音訊輸入 / 輸出端點           |
| Mixer    | 混音控制節點                  |
| PGA      | 可程式化增益放大器（前級）     |
| Supply   | 電源依賴節點（Power dependency）|

## 10. Supply Widget

例如：

`SND_SOC_DAPM_SUPPLY("VREF", REG, BIT, 0, NULL, 0),` 

如果 route 沒接 supply：

DAC 可能不會開。

## 11. Machine driver 也會加 routing

Machine driver 裡：
```
static  const  struct  snd_soc_dapm_route  audio_map[] = {
    {"Headphone Jack", NULL, "HPLOUT"},
};
```
這是 board-level routing。

## 12. 完整播放時 DAPM 動作

```
snd_soc_dapm_stream_event()
  ↓ dapm_power_widgets()
  ↓ dapm_seq_run()
  ↓
更新 register
```

## 13. 常見問題與排查

### 13.1 BSP Debug Checklist

如果沒聲音：

#### Step 1

`aplay 有錯嗎？` 

#### Step 2

`/proc/asound/cards 有嗎？` 

#### Step 3

`I2S clock 正常嗎？` 

#### Step 4

`cat /sys/kernel/debug/asoc/dapm` 

看 DAC 有沒有 ON。

#### Step 5

`amixer controls` 

確認 mixer 沒 mute。

### 13.2 真實世界常見錯誤

#### 忘記 machine routing

Codec 有 DAC，但 machine 沒接到 speaker。

#### supply 沒接

DAC power 永遠 off。

#### Endpoint 名稱不一致

Widget 名稱大小寫錯誤。

DAPM 找不到。

## 14. 心智模型總結

ASoC 音訊成功條件：

`PCM 正常 + DAI link 正確 + Clock 正確 + DAPM graph 完整` 

只缺一個都會沒聲音。
