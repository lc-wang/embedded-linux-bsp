# i.MX95 MAX98090 耳麥錄音音量過小分析（MIC boost 在 DAPM 上電時被重設為 0 dB）

## 1. 問題概述

一片 i.MX95 SMARC 自製板移植到 Android 16 後，耳麥的播放正常，但錄音非常小聲。對著耳麥正常說話，錄到的音量只有約 -36 dBFS，回放幾乎聽不清楚。

已知的現象：

- 播放（耳機輸出）的音量和左右聲道都正常，問題只在錄音。
- 音效 HAL 的設定檔有設 `MIC1 Boost Volume = 2`（+30 dB），開機後讀這個 control 也確實是 2。
- 每次錄完音再讀，`MIC1 Boost Volume` 變成 0。
- 同一塊板在 Yocto 上跑 test-framework 的 audio 測項是 PASS。

一開始以為是增益設太小，或音效 HAL 沒有套用設定。實際上設定有寫進去，**是 codec driver 在每次開始錄音、麥克風上電時，把 boost 覆蓋成 0 dB**。這個 bug 從 2013 年 driver 進 mainline 時就存在，mainline 7.4-rc3 仍未修正。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP android-16.0.0_2.0.0 |
| Kernel | 6.18.38（Android），Yocto 使用 6.18.37，mainline 對照 7.4-rc3 |
| Codec | MAX98090，I2C 接在 LPI2C5（`4-0010`），資料線走 SAI3 |
| Machine driver | `fsl-asoc-card`（`fsl,imx-audio-max98090`），`CONFIG_SND_SOC_MAX98090=y` |
| 音訊路由 | `Headphone Jack` ← HPL/HPR，`IN12` ← AMIC ← MICBIAS（耳麥麥克風走 MIC1） |
| 載板 | 只有耳麥孔，沒有喇叭 |

Yocto 的 `asound.state` 對 MAX98090 的設定（Android 的音效 HAL 設定照抄這些值）：

| Control | 值 |
|---|---|
| `MIC1 Boost Volume` | 2（+30 dB） |
| `MIC1 Volume` | 20（+20 dB） |
| `Left/Right ADC Mixer MIC1 Switch` | on |
| `MIXHPLSEL` / `MIXHPRSEL` | DAC Only |
| `Headphone Volume` | 22 |

## 3. 除錯過程

### 3.1 量化錄音音量

先錄 10 秒（前段安靜、後段說話），逐秒計算 RMS：

```bash
tinycap /data/local/tmp/mic.wav -D 0 -d 0 -c 2 -r 48000 -b 16 -T 10
```

| 狀態 | 說話時 | 安靜時 |
|---|---|---|
| 修正前 | 約 -36 dBFS | 約 -52 dBFS |

聽得出是人聲，但比一般錄音（約 -20 到 -12 dBFS）低了將近 20 dB。

### 3.2 設定有沒有寫進 codec

`MIC1 Boost Volume` 的讀值會誤導，因為 driver 在麥克風斷電時回傳的是快取值，不是暫存器。所以改成直接看 regmap 的暫存器 0x10（MIC1 Input Level，PA1EN 在 bit 6:5），並在錄音前、中、後各讀一次：

```bash
R=/sys/kernel/debug/regmap/4-0010/registers
tinymix -D 0 "MIC1 Boost Volume" 2
grep "^10:" $R
(tinycap /data/local/tmp/x.wav -D 0 -d 0 -c 2 -r 48000 -b 16 -T 3 &)
sleep 1.5; grep "^10:" $R; tinymix -D 0 "MIC1 Boost Volume"
sleep 3;   grep "^10:" $R; tinymix -D 0 "MIC1 Boost Volume"
```

| 時間點 | 暫存器 0x10 | PA1EN | `MIC1 Boost Volume` 讀值 |
|---|---|---|---|
| 設成 2 之後（未錄音） | `0x00` | 0（關） | 2 |
| 錄音中 | `0x20` | 1（0 dB） | 0 |
| 錄音結束 | `0x00` | 0（關） | 0 |

錄音中 PA1EN 是 1，等於 0 dB，預期應該是 3（+30 dB，`0x60`）。而且錄完之後，連快取值也變成 0。所以設定確實寫進去了，是開始錄音時被改掉的。

### 3.3 為什麼 Yocto 的 audio 測項會 PASS

Yocto 的 alsa-state 也是在開機時、麥克風還沒上電時還原 `MIC1 Boost Volume`，條件和 Android 一樣，所以 Yocto 上的 boost 實際上也是 0 dB。測項沒抓到，是因為它的測法量不到這件事：

- **測法是迴路（loopback）**：用迴路線把耳機輸出接回麥克風輸入，`speaker-test` 播 1 kHz，同時 `arecord` 錄音。
- **只看相對訊雜比**：1 kHz 峰值比 noise floor 高 25 dB 以上（`AUDIO_TONE_MIN_DB=25`）就 PASS。`tone_dbfs` 只印在 log 裡，不參與判斷。
- **沒有設定 `MIC1 Boost Volume`**，也不檢查錄音的絕對音量。
- **迴路進來的是 line 等級的強訊號**，即使 PA1EN 只有 0 dB，SNR 也遠高於 25 dB。

真人對著耳麥說話的訊號弱很多，才會在 0 dB 下顯得很小聲。

### 3.4 Mainline 與其他回報

- 這段程式碼從 `685e42154dcf`（"ASoC: Replace max98090 Device Driver"，2013）之後就沒有人改過。`git log -S "Update for volatile"` 只找得到這一筆。
- Android 6.18.38、Yocto 6.18.37、mainline 7.4-rc3 的 `max98090_micinput_event()` 完全相同。
- 2018 年 bringmeup 專案（i.MX6 + Android）回報過同樣的症狀：MIC1/MIC2 Boost 控制「不會生效」，把暫存器 0x10 寫成 `0x60` 讀回來卻是 `0x20`。他們在自己的 fork 改成直接寫暫存器的 enum control（`fade2f4fea25`），沒有送 upstream。

## 4. Root Cause 分析

### 4.1 背景與術語

PA1EN（MIC1）和 PA2EN（MIC2）是 2 bit 的欄位，同時代表前級放大器的開關和增益：

| PA1EN | 意義 | `MIC1 Boost Volume` |
|---|---|---|
| 0 | 關閉 | （無） |
| 1 | 0 dB | 0 |
| 2 | +20 dB | 1 |
| 3 | +30 dB | 2 |

因為「關閉」也放在同一個欄位，driver 必須自己記住使用者選的增益，由三個地方配合：

```text
max98090_put_enab_tlv()        設定 "MIC1 Boost Volume"
├─ 把選擇存進 max98090->pa1en
└─ 麥克風關閉時不寫暫存器（寫了就等於把麥克風打開）

SND_SOC_DAPM_PGA_E("MIC1 Input", M98090_REG_MIC1_INPUT_LEVEL,
                   M98090_MIC_PA1EN_SHIFT, ...)
└─ DAPM 只把它當成 1 bit 的 enable（bit 5，PA1EN 的低位元）

max98090_micinput_event()      DAPM 事件
├─ 讀回暫存器，同步到 pa1en（"Update for volatile"）
├─ POST_PMU：寫入 pa1en + 1
└─ POST_PMD：寫入 0
```

| 術語 | 白話 |
|---|---|
| DAPM | ASoC 的動態電源管理，依音訊路徑是否在用，自動開關 widget |
| widget | DAPM 管理的元件；`MIC1 Input` 有對應的暫存器 bit，上電時設 1、斷電時清 0 |
| POST_PMU / POST_PMD | DAPM 改完 widget 的暫存器 bit **之後**才呼叫的事件 |
| PRE_PMD | DAPM 改暫存器 bit **之前**呼叫的斷電事件 |
| volatile | 暫存器的值可能被 control 以外的路徑改掉，所以要讀回來同步 |

### 4.2 根因

以 6.18.38 / 7.4-rc3 的原始碼為準：

```c
static int max98090_micinput_event(struct snd_soc_dapm_widget *w,
				 struct snd_kcontrol *kcontrol, int event)
{
	...
	unsigned int val = snd_soc_component_read(component, w->reg);

	if (w->reg == M98090_REG_MIC1_INPUT_LEVEL)
		val = (val & M98090_MIC_PA1EN_MASK) >> M98090_MIC_PA1EN_SHIFT;
	...
	if (val >= 1) {
		if (w->reg == M98090_REG_MIC1_INPUT_LEVEL) {
			max98090->pa1en = val - 1; /* Update for volatile */
		...
	}

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		/* If turning on, set to most recently selected volume */
		if (w->reg == M98090_REG_MIC1_INPUT_LEVEL)
			val = max98090->pa1en + 1;
		...
	case SND_SOC_DAPM_POST_PMD:
		/* If turning off, turn off */
		val = 0;
	...
}
```

這段程式碼的用意是：先把暫存器的實際增益同步回 `pa1en`，再依事件寫入。設計方向沒錯，錯在讀的時機。事件只註冊了 POST_PMU 和 POST_PMD，兩個都在 DAPM 改過暫存器之後：

```text
開始錄音（麥克風上電）
  DAPM 把 bit 5 設為 1          PA1EN: 0 → 1（0 dB）
  POST_PMU 事件
  ├─ 讀回 PA1EN = 1
  ├─ pa1en = 1 - 1 = 0          使用者選的 +30 dB 被蓋掉
  └─ 寫入 pa1en + 1 = 1         錄音以 0 dB 進行

停止錄音（麥克風斷電）
  DAPM 把 bit 5 清為 0          PA1EN: 3 → 2（若當時是 +30 dB）
  POST_PMD 事件
  ├─ 讀回 PA1EN = 2
  └─ pa1en = 1                  +30 dB 被記成 +20 dB
```

事實整理：

1. 麥克風關閉時設定的 boost（開機時由 alsa-state 或音效 HAL 還原），每次開始錄音都會被 POST_PMU 覆蓋成 0 dB，之後就一直維持 0 dB。
2. 只有在錄音進行中設定 boost，才會直接寫進暫存器並生效。這也是為什麼這個 bug 不容易被發現：如果系統是在開始錄音之後才套用 mixer 設定，就剛好繞過去。
3. 即使只把同步移到 POST_PMD 也不對，因為 DAPM 已經清掉 bit 5，+30 dB 會被記成 +20 dB。

## 5. 解決方案

### 5.1 修正 patch

把讀回同步移到 PRE_PMD（DAPM 清 bit 之前，暫存器還是完整的增益），POST_PMU 只寫回記住的值：

```diff
@@ static int max98090_micinput_event(struct snd_soc_dapm_widget *w,
 	else
 		val = (val & M98090_MIC_PA2EN_MASK) >> M98090_MIC_PA2EN_SHIFT;

-	if (val >= 1) {
-		if (w->reg == M98090_REG_MIC1_INPUT_LEVEL) {
-			max98090->pa1en = val - 1; /* Update for volatile */
-		} else {
-			max98090->pa2en = val - 1; /* Update for volatile */
-		}
-	}
-
 	switch (event) {
 	case SND_SOC_DAPM_POST_PMU:
 		/* If turning on, set to most recently selected volume */
@@ static int max98090_micinput_event(struct snd_soc_dapm_widget *w,
 		else
 			val = max98090->pa2en + 1;
 		break;
+	case SND_SOC_DAPM_PRE_PMD:
+		/* Update for volatile before DAPM clears the enable bit */
+		if (val >= 1) {
+			if (w->reg == M98090_REG_MIC1_INPUT_LEVEL)
+				max98090->pa1en = val - 1;
+			else
+				max98090->pa2en = val - 1;
+		}
+		return 0;
 	case SND_SOC_DAPM_POST_PMD:
 		/* If turning off, turn off */
 		val = 0;
@@ static const struct snd_soc_dapm_widget max98090_dapm_widgets[] = {
 	SND_SOC_DAPM_PGA_E("MIC1 Input", M98090_REG_MIC1_INPUT_LEVEL,
 		M98090_MIC_PA1EN_SHIFT, 0, NULL, 0, max98090_micinput_event,
-		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
+		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |
+		SND_SOC_DAPM_POST_PMD),

 	SND_SOC_DAPM_PGA_E("MIC2 Input", M98090_REG_MIC2_INPUT_LEVEL,
 		M98090_MIC_PA2EN_SHIFT, 0, NULL, 0, max98090_micinput_event,
-		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_POST_PMD),
+		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD |
+		SND_SOC_DAPM_POST_PMD),
```

保留原本「同步暫存器實際值」的設計，錄音中改過的 boost 下次錄音仍會沿用。control 名稱和介面都不變，alsa-state 和音效 HAL 不需要改。

### 5.2 考慮過但沒採用的做法

| 做法 | 問題 |
|---|---|
| 整段刪掉讀回同步 | 能修好，但錄音中途改過的值不會被記住，偏離原本的設計 |
| 只把讀回移到 POST_PMD | DAPM 已清 bit 5，+30 dB 會被記成 +20 dB（見 §4.2） |
| 改成直接寫暫存器的 enum control（bringmeup 的做法） | 改動 control 介面，現有的 alsa-state、UCM、音效 HAL 設定都要跟著改 |
| 只在 Android 調高 ADC 增益 | 沒修到 bug，MIC1 Boost 仍然無效，Yocto 也沒解決 |

### 5.3 Android 音效 HAL 設定

MAX98090 原本在 Android 上沒有音效 HAL 設定檔，mixer 通路全部是關的，耳機也不會出聲。新增 `max98090_config.json`，`init_ctl` 照抄 Yocto `asound.state` 的值（見 §2），安裝到 `/vendor/etc/configs/audio/`。音效 HAL 以音效卡名稱 `max98090-audio` 比對這個檔案。

### 5.4 驗證結果

| 項目 | 方法 | 結果 |
|---|---|---|
| 錄音前設 boost = 2，連錄兩次 | 錄音中讀暫存器 0x10 | 兩次都是 `0x60`（+30 dB），錄完讀值仍是 2 |
| 錄音前改 boost = 1 | 同上 | `0x40`（+20 dB） |
| 錄音中途改 boost = 2 | 同上 | 立刻變 `0x60`，下一次錄音仍是 `0x60` |
| 耳麥錄人聲 | `tinycap` 10 秒，逐秒 RMS | 說話 -13 到 -23 dBFS（修正前約 -36 dBFS），沒有 clipping，回放清楚 |
| 耳機播放 | 左右聲道輪流的測試音、wav / mp3 | 左右正確，沒有雜音 |

## 6. 上游狀態

- Mainline 7.4-rc3 尚未修正。patch 已用 `b4 prep` 整理好（基於 `b3dfbde62cf1`），checkpatch 沒有 error 和 warning，待送 linux-sound。
  - `Fixes: 685e42154dcf ("ASoC: Replace max98090 Device Driver")`
  - 收件人：Liam Girdwood、Mark Brown、Jaroslav Kysela、Takashi Iwai，Cc linux-sound、linux-kernel。
- 先前的回報：bringmeup/linux-imx6 `fade2f4fea25`（2018），只在私有 fork，沒有送 upstream。
- Android 6.18 BSP 已套用同一個修正。Yocto 6.18 分支有同樣的問題，需要 cherry-pick。

## 7. 結論與建議

1. **讀回暫存器的時機要對。** DAPM 的 POST_PMU / POST_PMD 是在 DAPM 改完 widget 的 bit 之後才呼叫。要讀「使用中」的值，必須在 PRE_PMD，或在 widget 上電期間讀。
2. **一個欄位同時當開關和增益時要特別小心。** DAPM 只會動它的 1 個 bit，其他 bit 的意義由 driver 自己維護，最容易出這種錯。
3. **不要只信 control 的讀值。** 這個 driver 在麥克風斷電時回傳快取值，讀到 2 不代表硬體是 +30 dB。要在錄音中直接看 regmap 暫存器。
4. **迴路測試量不到增益問題。** 只看 SNR 的 loopback 測項，在增益錯了 30 dB 時仍然會 PASS。建議 audio 測項加上絕對音量門檻（`tone_dbfs` 下限），或加一個真人對麥克風說話的測項。
5. **長期沒人回報的 bug 不一定不存在。** 這個 bug 存在超過十年，可能是因為多數平台在開始錄音後才套用 mixer 設定，剛好繞過。開機時一次還原 mixer 的系統（alsa-state、Android 音效 HAL）都會遇到。

## 附錄

### A. 常用指令

```bash
cat /proc/asound/cards
tinymix -D 0 | grep -E "MIC1|ADC|Headphone"
tinymix -D 0 "MIC1 Boost Volume"

grep -E "^(02|10|13):" /sys/kernel/debug/regmap/4-0010/registers

tinycap /data/local/tmp/mic.wav -D 0 -d 0 -c 2 -r 48000 -b 16 -T 10
tinyplay /data/local/tmp/tone.wav -D 0 -d 0

logcat -s AHAL_AudioCardManager
```

### B. 參考資料

- Linux：`sound/soc/codecs/max98090.c`（`max98090_micinput_event`、`max98090_get_enab_tlv`、`max98090_put_enab_tlv`）
- Linux：`sound/soc/codecs/max98090.h`（`M98090_MIC_PA1EN_MASK`、`M98090_MIC_PA1EN_SHIFT`）
- Linux commit `685e42154dcf`：ASoC: Replace max98090 Device Driver
- bringmeup/linux-imx6 commit `fade2f4fea25`：ASoC: max98090: Fix problem with analog MIC gain control
- MAX98090 datasheet：MIC1/MIC2 Input Level 暫存器（0x10 / 0x11）
