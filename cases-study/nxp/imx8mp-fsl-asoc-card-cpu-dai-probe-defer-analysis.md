# i.MX8MP Android 類比音效卡開機不註冊分析（fsl-asoc-card CPU DAI probe 順序回歸 → EPROBE_DEFER）

## 1. 問題概述

一片 i.MX8MP 板子移植到 NXP android-16.0.0_2.0.0（kernel 6.18）後，類比音效卡（ALC5672，接在 SAI3）**開機不會註冊**：只有 HDMI 音效卡在，`/proc/asound/cards` 沒有 alc5672。連帶影響：

- 喇叭、耳麥（HP/MIC）皆無聲。
- **相機錄影失敗** —— MediaRecorder 需要音源，整張類比卡不見 → 錄影無法建立。

`dmesg` 在開機極早期（~0.9s）就報錯並放棄 probe：

```
fsl-asoc-card sound-alc5672: failed to find CPU DAI device
fsl-asoc-card sound-alc5672: probe of sound-alc5672 failed with error -22
```

`-22` = `-EINVAL`，是**永久失敗**：driver core 不會重試，即使負責的 SAI platform device 稍後才出現，音效卡也不會再註冊。

關鍵背景:同一份 dts、同一顆 codec、同一個 machine driver，在**前一版 Android（Android 15 / kernel 6.6）上開機即正常**。所以這不是 dts 或 codec 設定問題。

## 2. 系統環境

| 項目 | 出問題（a16） | 對照可用（a15） |
|---|---|---|
| 板子 | 某 i.MX8MP 自製板 | 同一片 |
| BSP | NXP android-16.0.0_2.0.0 | NXP android-15 |
| Kernel | 6.18.21 | 6.6.56 |
| Android | 16 | 15 |
| Codec | ALC5672（rt5670 family）@ I2C，資料線走 **SAI3** | 同 |
| Machine driver | `fsl-asoc-card`（`select` `SND_SOC_RT5670`，故 codec driver 一併被編） | 同 |
| 建置模式 | 單體式（`LOADABLE_KERNEL_MODULE := false`，非 GKI） | 同 |

音訊相關 device tree 節點（兩版相同）:

| 節點 | compatible / 內容 |
|---|---|
| machine | `fsl,imx-audio-rt5670`，`audio-cpu = <&sai3>`，`audio-codec = <&rt5672>` |
| cpu-dai | `sai3`（`fsl,imx8mp-sai`）platform device |
| codec | `rt5672@1c`（i2c） |

## 3. 除錯過程

### 3.1 排除環境因素

| 假設 | 驗證方式 | 結果 |
|---|---|---|
| codec i2c driver 沒載 | `ls /sys/bus/i2c/drivers/rt5670/` | 已 bind，codec 本身正常 |
| SAI3 沒 enable / dts 錯 | 比對 a15 vs a16 dts 音訊節點 | **完全相同**，非 dts 問題 |
| defconfig 少了 codec | machine driver `select` codec，`fsl-asoc-card` 有被編 | 非 config 缺漏 |
| 硬體 / kernel driver 有問題 | 同顆 kernel 在 **Yocto**（非 Android）建置下耳麥有聲、可錄音 | 硬體 + driver 正常，問題在 Android 這側的時序/HAL |
| 純粹 Android 版本差 | 同一片板子燒 a15 → 開機音效卡正常註冊 | 縮小到 **kernel 版本 / probe 時序** |

### 3.2 Workaround 嘗試：改成 `=m`（失敗，反而 boot loop）

Yocto 建置採 `=m`，於是嘗試 `CONFIG_SND_SOC_FSL_ASOC_CARD=m` 並把 `.ko` 加進 `BOARD_VENDOR_RAMDISK_KERNEL_MODULES`：

```
init: Failed to insmod '/.../snd-soc-rt5670.ko': No such file or directory
init: FatalReboot: signal 6 (InitFatalReboot)
```

原因:單體式 Android 只載 `modules.load` 清單裡的 `.ko`，且 **init 把任何 insmod 失敗視為 FATAL → 直接 boot loop**。`rt5670` 還相依 `snd-soc-rl6231.ko`（不在清單），漏一個就崩。Yocto 之所以能 `=m`，是因為它的 module 晚載、失敗不致命；Android 這裡不行。**→ 結論:不能靠 `=m`，要在 source 端讓 built-in driver 正確 defer。**

### 3.3 關鍵對照實驗:同板 a15 vs a16

| 實驗 | Kernel | 開機時 alc5672 卡 | 說明 |
|---|---|---|---|
| 燒 a15 | 6.6.56 | **有註冊** ✓ | SAI platform device 先於 machine driver 被 populate |
| 燒 a16 | 6.18.21 | **不註冊** ✗ | machine driver 先跑，找不到 SAI → -EINVAL |

同一片板子、同 dts，只有 kernel 版本不同 → **證實是 probe 順序（版本相依）**，非硬體/設定。

### 3.4 順序證明（開機後手動 bind，不必重編）

開機完成後（此時 SAI platform device 早已存在）手動觸發 machine driver:

```
echo sound-alc5672 > /sys/bus/platform/drivers/fsl-asoc-card/bind
```

→ 音效卡**立刻註冊成功**。這證明 driver 邏輯與硬體都對，唯一的問題是**開機當下 machine driver 比 SAI 早 probe**。

### 3.5 額外調查:兩個「假缺陷」

音訊測試過程中另外冒出兩個看似缺陷、實則是 bring-up 治具造成的假象，記錄於此以免後人重踩。

#### 3.5.1 media 音訊「跑去 HDMI、內建喇叭沒聲」

- 現象:框架播放（app）無聲，但 `tinyplay` 有聲。
- 佐證:`dumpsys media.audio_flinger` 顯示 **HDMI 輸出 thread `Frames written` > 0 / `Standby: no`**、SPEAKER thread `Standby: yes`；`cmd audio get-current-output-device` = HDMI。
- 根因:bring-up 接的是一台**外接桌面螢幕**（有內建喇叭），其 EDID CTA 宣告 **Basic audio = true**。Android 預設 policy engine 對 `media` strategy 把有音訊能力的 HDMI 排在內建喇叭之前 → 正確地送去 HDMI。
- **非回歸、非 bug**:同片板子燒 a15，media 一樣落在 HDMI 輸出 thread（`Standby: no`），a15/a16 行為相同；兩版 `audio_policy_configuration.xml` 逐字一致。
- Production（內建面板、無 HDMI 音訊 sink）→ media 自然走內建喇叭。若要在接 HDMI 時仍強制走喇叭:a16+ 可 `cmd audio set-preferred-output-device BUILTIN_SPEAKER`（runtime、非持久;Java `setForceUse(FOR_MEDIA,…)` 被 AudioService 擋掉，故無乾淨的烤進 image 旋鈕，除非改用 configurable engine / product-strategies）。

#### 3.5.2 耳機「左右聲道對調」

- 現象:左右分離測試音（前 4 秒左聲道、後 4 秒右聲道），戴耳機聽成「先右後左」。
- 誤判過程:一度以為是板子 HP 走線反接，並在 `Stereo DAC MIXL/MIXR` 做 L/R 對調補償（`tinymix` live 測試「修好」了）。
- 翻案:換**第二支標準耳機**、用**原始未改** codec 路由重測 → 前 4 秒（左聲道）進**左耳**，完全正常。
- 定論:codec 內部路由本就正常（`DAC L1 → Stereo DAC MIXL → HPOVOL MIXL → HPOL`）。codec 端的 L/R swap 位於 **jack 之前**，會對任何耳機一律翻轉;兩支耳機在同一設定下結果**相反** → 必然是**其中一支耳機反接**，不是板子。第一支測試耳麥 L/R 內部接反才是元兇。修改已還原，config 維持原狀。

**教訓:懷疑周邊缺陷時，先對 production 硬體、並用「已知正常」的周邊驗證;bring-up 治具（外接螢幕、備用耳機）會製造假缺陷。**

## 4. Root Cause 分析

### 4.1 背景與術語

```
of_platform_populate() 依 device tree 建立 platform_device — 順序「非框架保證」，且跨 kernel 版本改變

  6.6 :  … → SAI3 platform_device → … → sound-alc5672(machine)
                    (先建)                       (後 probe) → of_find_device_by_node(SAI) 找得到 ✓ → 註冊音效卡

  6.18:  … → sound-alc5672(machine) → … → SAI3 platform_device
                    (先 probe) → of_find_device_by_node(SAI)=NULL → 回 -EINVAL ✗（永久失敗，core 不重試）
                                                          (後才建，但太遲了)
```

| 術語 | 白話 |
|---|---|
| machine driver | 把「CPU 端 DAI（SAI）＋ codec」綁成一張音效卡的膠水 driver（此處 `fsl-asoc-card`） |
| CPU DAI | 數位音訊介面的 SoC 端，即 SAI3 對應的 platform device |
| `of_find_device_by_node()` | 用 dts 節點反查已建立的 platform_device；**該裝置還沒被 populate 時回 NULL** |
| `-EPROBE_DEFER` | 「相依還沒好，等會再叫我」—— driver core 會**稍後重試** probe |
| 永久 `-EINVAL` | 「這個 probe 廢了」—— core **不重試**，音效卡就此消失 |

### 4.2 根因

`fsl_asoc_card_probe()` 從 `audio-cpu` phandle 解出 SAI 節點，再反查其 platform device；找不到時**硬回 `-EINVAL`**:

```c
cpu_pdev = of_find_device_by_node(cpu_np);
if (!cpu_pdev) {
        dev_err(&pdev->dev, "failed to find CPU DAI device\n");
        ret = -EINVAL;          /* ← 永久失敗，不 defer */
        goto asrc_fail;
}
```

事實整理:

1. `of_platform_populate` 建立 platform_device 的順序不被框架保證，且 6.6→6.18 之間改變，導致 machine driver 可能早於 SAI probe。
2. 此時 `of_find_device_by_node()` 回 NULL，driver 回 `-EINVAL` → core 不再重試 → 音效卡永久缺席。
3. **同一函式的 codec 分支早就處理過這種情況**:commit `e396dec46c56`（"ASoC: fsl-asoc-card: Defer probe when fail to find codec device"，Shengjiu Wang, 2020）在找不到 codec 時回 `-EPROBE_DEFER`。
4. 但 **CPU DAI 分支從未加過 defer**:`git log -S "find CPU DAI device" -- sound/soc/fsl/fsl-asoc-card.c` 只有原始新增 `708b4351f08c`（2014）。codec 走 i2c（常見晚 probe）早年就被修；CPU DAI 是 SAI platform device（通常早），這個 latent bug 潛伏到 6.18 的 populate 順序改變才引爆。

## 5. 解決方案

### 5.1 修正 patch

比照既有的 codec-defer 前例，把 CPU DAI 分支從硬回 `-EINVAL` 改為 defer:

```diff
 	cpu_pdev = of_find_device_by_node(cpu_np);
 	if (!cpu_pdev) {
-		dev_err(&pdev->dev, "failed to find CPU DAI device\n");
-		ret = -EINVAL;
+		ret = dev_err_probe(&pdev->dev, -EPROBE_DEFER,
+				    "failed to find CPU DAI device\n");
 		goto asrc_fail;
 	}
```

如此 machine driver 若早於 SAI 被 probe，會被 driver core **稍後重試**，SAI 一出現即成功註冊。保持全部 `=y`（built-in），無 boot loop 風險。checkpatch clean、`allmodconfig` 編過（`CC [M] sound/soc/fsl/fsl-asoc-card.o` 無 warning）。

### 5.2 為何是 `=y` + defer，而非 `=m`

見 §3.2:單體式 Android 對 `=m` 的相依鏈極脆弱，漏一個 `.ko` 就 `InitFatalReboot`。`=y` + `-EPROBE_DEFER` 是既 robust 又不動建置模式的正解，也與 a15（6.6，同 driver `=y`）相容。

### 5.3 Android HAL config

音效卡起來後，NXP audio HAL 還需要對應的 per-codec json:

- `/vendor/etc/configs/audio/alc5672_config.json`（以音效卡 `driver_name = alc5672-audio` 比對），內含 ADC/DAC 路由 `init_ctl` 與 `out_volume_ctl`。
- 此檔 a15 已有現成 commit，用 **`git cherry-pick -x`** 帶進 a16（保留作者/provenance），`.mk` 的 `PRODUCT_COPY_FILES` copy 區塊因跨版本排版不同會衝突，resolve 成 a16 版即可。
- 板子 `audio_policy_configuration.xml`（`primary` module = 類比路徑）本來就正確，不需改。
- 附註:此板 jack-detect 未接 GPIO，故 `init_ctl` 強制常開 `"Headphone Jack"`/`"Mic Jack"`，讓 HP amp 與 mic bias 恆通電（Android 因此收不到耳機插拔事件，但 codec HP 一直有輸出）。

### 5.4 驗證結果

| 項目 | 方法 | 結果 |
|---|---|---|
| 開機自動註冊 | `cat /proc/asound/cards` | 出現 `card1: alc5672audio`（無需手動 bind） |
| 喇叭放音 | `tinyplay tone.wav -D 1 -d 0`（繞過框架，驗 codec/ALSA） | 單聲道內建喇叭有聲 |
| 麥克風收音 | `tinycap -D 1 -d 0 mic.wav` + 逐秒 RMS | 講話段 RMS ~0.035 vs 底噪 ~0.0002（SNR ≈ 46 dB） |
| 相機錄影 | Camera app 錄影 | 產出 MP4，含 `avc1` + `mp4a` 音軌 |
| wav/mp3 播放 | 框架播放 + 內建喇叭 | mp3 走 `c2.imx.mp3` decoder，正常出聲 |

喇叭放音、wav/mp3 播放、麥克風（AMIC）收音三項功能皆通過驗證。

## 6. 上游狀態

- 主修正 patch（CPU DAI defer）已送 mainline linux-sound。
  - `Fixes: 708b4351f08c ("ASoC: fsl: Add Freescale Generic ASoC Sound Card with ASRC support")`
  - 前例:`e396dec46c56 ("ASoC: fsl-asoc-card: Defer probe when fail to find codec device")`（codec 分支，Shengjiu Wang, 2020）。

## 7. 結論與建議

1. **built-in driver 遇到「相依尚未 populate」時，應回 `-EPROBE_DEFER`（`dev_err_probe`），不要硬回 `-EINVAL`。** `of_platform_populate` 的順序非框架保證且跨版本會變，任何 `of_find_device_by_node()` 找不到就放棄的 probe 都是潛在的版本相依 bug。此類問題常在 kernel 升版（如 6.6→6.18）後才浮現。
2. **單體式 Android 不要用 `=m` 迴避 probe 問題** —— 相依鏈漏一個 `.ko` 即 `InitFatalReboot`。用 source 端 defer。
3. **周邊「缺陷」先對 production 硬體與已知正常周邊驗證。** 本案兩個看似音訊 bug（media→HDMI、HP L/R）最終都是治具假象（外接桌面螢幕、反接耳麥），非板子問題。以「同板前一版 Android 是否相同行為」快速區分回歸 vs 既有特性。
