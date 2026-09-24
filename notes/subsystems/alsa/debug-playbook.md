# ALSA / ASoC Debug Playbook

> SoC 音訊 Bring-up 流程排錯手冊

當遇到：

-   aplay 正常但沒聲音

-   錄音無資料

-   聲音破音

-   只有一邊有聲音

-   48kHz 正常 44.1kHz 壞掉

你可以快速分類問題。

## 1. 音訊問題分類模型

音訊問題通常分成四大類：
```
[1] Card 沒建立
[2] PCM 無法播放
[3] PCM 正常但沒聲音
[4] 有聲音但異常
```

## 2. Case A：Sound Card 根本沒出現

### 2.1 症狀

`aplay -l` 

沒有任何 card。

### 2.2 檢查步驟

#### Step 1

`dmesg | grep -i asoc` 

#### Step 2

確認 driver probe：
```
CPU DAI driver probe ?
Codec driver probe ?
Machine driver probe ?` 
```

#### Step 3

檢查 DAI link：

`ASoC:  no  DAI  found` 

#### 常見原因

-   dai_link 名稱錯

-   codec 未註冊

-   DTS compatible 錯

-   simple-audio-card binding 錯

## 3. Case B：PCM 打不開

### 3.1 症狀

`aplay: device busy  aplay: invalid argument` 

### 3.2 檢查

```
cat /proc/asound/cards
cat /proc/asound/pcm
```

#### 常見原因

-   channels 不支援

-   sample rate 不支援

-   format 不支援

-   .rates / .formats 設定錯

## 4. Case C：PCM 正常但沒聲音

這是 BSP 地獄。

### 4.1 Step 1：確認 DMA 有跑

看：

`/proc/asound/pcm` 

或：

`cat /proc/interrupts | grep i2s` 

### 4.2 Step 2：示波器量 I2S

量：

-   BCLK

-   LRCLK

-   DATA

如果沒有 clock：

→ hw_params 沒設好

### 4.3 Step 3：看 DAPM

`cat /sys/kernel/debug/asoc/dapm` 

確認：
```
DAC ON ?
Output ON ?
Supply ON ?
```
如果 DAC 是 OFF：

→ routing 錯

### 4.4 Step 4：看 mixer

```
amixer scontrols
amixer scontents
```
看：

-   mute ?

-   volume 0 ?

### 4.5 Step 5：量 speaker enable GPIO

很多 machine driver 會：

`gpio_set_value(spk_en, 1)` 

GPIO 沒開也會沒聲音。

## 5. Case D：有聲音但異常

### 5.1 問題 1：雜音

常見原因：

-   BCLK mismatch

-   bit width mismatch

-   codec PLL 設錯

### 5.2 問題 2：聲音很小

-   Mixer 未開

-   PGA 未開

-   gain 太低

### 5.3 問題 3：左右聲道顛倒

-   routing 錯

-   DAI format 設錯

### 5.4 問題 4：pop noise

-   power sequence 錯

-   DAPM event timing 不對

-   speaker enable 太早

## 6. Bring-up 標準流程

不要亂 debug。

照這個順序：

### 6.1 確認 Card 存在

`aplay -l` 

### 6.2 確認 PCM 可開

`aplay test.wav` 

### 6.3 看 interrupts

`cat /proc/interrupts` 

### 6.4 量 I2S clock

示波器。

### 6.5 看 DAPM

`/sys/kernel/debug/asoc/dapm` 

### 6.6 看 mixer

`amixer` 

### 6.7 量類比輸出

確認 DAC 有輸出。

## 7. 常見真實錯誤

DAPM routing 少一段

Master/slave 設錯

Codec PLL 設錯

Speaker enable GPIO 沒開

DTS format mismatch

## 8. 快速決策樹

```
沒聲音？
  ↓
Card 有嗎？
  ↓
PCM 有跑嗎？
  ↓
I2S 有 clock？
  ↓
DAPM DAC ON？
  ↓
Mixer unmute？
  ↓
GPIO speaker enable？
```
只要按這個順序，幾乎一定找得到問題。

## 9. 高階 Debug 技巧

### 9.1 打開 dynamic debug

`echo  'file sound/soc/* +p' > /sys/kernel/debug/dynamic_debug/control` 

### 9.2 在 hw_params 加 printk

看 sample rate 是否正確。

### 9.3 追蹤 dapm power event

`echo 1 > /sys/kernel/debug/asoc/dapm_debug` 

## 10. 心智模型總結

ASoC 成功條件：

`Driver probe OK + DAI link OK + Clock OK + DAPM graph OK + Mixer OK + GPIO OK` 

缺一不可。
