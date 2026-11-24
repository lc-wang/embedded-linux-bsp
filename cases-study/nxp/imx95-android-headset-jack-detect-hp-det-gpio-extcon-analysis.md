# i.MX95 Android 耳麥插拔偵測分析（codec JACKSNS 不可用，改用載板 HP_DET GPIO + extcon）

## 1. 問題概述

一片 i.MX95 SMARC 自製板移植到 Android 16 後，替 HDMI（MIPI-DSI 經 LT9611UXD 轉 HDMI）加上音訊輸出。HDMI 音訊接通之後出現新問題：**只要 HDMI 螢幕接著，耳麥插上去也聽不到聲音，Android 一直把媒體送到 HDMI。**

已知的現象：

- 沒接 HDMI 時，耳麥播放、錄音都正常。
- 接上 HDMI 後，插拔耳麥，`dumpsys audio` 的 Connected devices 只有 `hdmi`，不會出現耳機。
- codec（MAX98090）本身有偵測到插拔：JDET 中斷會觸發，JACK_STATUS 暫存器會變。
- Yocto 上同一塊板子的耳麥測項一直是 PASS。

一開始以為只要把 codec 的偵測結果往上報就好。實際上有三層問題：

1. **Android 要的是 extcon（或 input event），kernel 兩者都沒有回報。** codec 的 IRQ 只在 kernel 內切換路徑，沒有往 userspace 報。
2. **這塊載板的 codec JACKSNS 不能用來判斷插拔。** 沒插時讀到 `0x00`，upstream 的 jack 判斷會把它當成「耳機已插」或「耳麥按鍵按下」。真正可靠的來源是載板上獨立的機械式偵測開關 `Audio_HP_DET`（SoM `GPIO4_IO01`，插入為 high）。
3. **NXP fsl-asoc-card 在音效卡註冊之後才建立 jack**，所以只有 ALSA control，沒有 input device，也就沒有 `SW_HEADPHONE_INSERT`。

最後的做法：dts 用 `hp-det-gpios` 描述載板的偵測開關，沿用 NXP fsl-asoc-card 既有的 jack → extcon 路徑（MA-12281），再把 Yocto 端修好的「jack 移到 late_probe」移植過來。codec driver 不加任何程式碼。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP android-16.0.0_2.0.0 |
| Kernel | 6.18（Android 分支）；Yocto kernel 為 6.18.37 分支，兩邊 dts 共用 |
| Codec | MAX98090，I2C `4-0010`，中斷 `GPIO4_IO00`，資料線走 SAI3 |
| Machine driver | `fsl-asoc-card`（`fsl,imx-audio-max98090`），音效卡 `max98090-audio`，dts 節點 `sound-max98090` |
| HDMI | MIPI-DSI → LT9611UXD（out-of-tree module，Android 與 Yocto 共用），音訊走 `simple-audio-card` + hdmi-codec，音效卡 `sound-hdmi` |
| 耳麥孔 | 載板只有一個 4 極耳麥孔，沒有喇叭、沒有內建麥克風 |
| 耳機偵測 | 耳麥孔 pin 4 `HP_DET_C` → Q76 → `Audio_HP_DET`（SMARC GPIO8 / P117）→ SoM `GPIO4_IO01`（pad `ENET1_MDIO`），插入 = high |
| Android 端 | `WiredAccessoryManager` 讀 `/sys/class/extcon`；`CONFIG_EXTCON=y`（板子專用 kernel fragment） |

## 3. 除錯過程

### 3.1 HDMI 音訊接通後，耳麥沒聲音

HDMI 音訊的做法比照 NXP 在 i.MX8MP（dw-hdmi）上的方式：

| 層 | 改動 |
|---|---|
| LT9611UXD driver | 註冊 `EXTCON_DISP_HDMI`，HPD work 和 `bridge_detect` 時更新狀態 |
| device/nxp | `hdmi_config.json`（`driver_name: "sound-hdmi"`）、audio policy 加 `hdmi output` mixPort、`HDMI Out` devicePort 和 route |
| SELinux | `1-0041/extcon` 標成 `sysfs_extcon` |

最早的版本把 `HDMI Out` 放進 policy 的 `attachedDevices`（當成永遠連接），結果出問題，改成靠 extcon 動態連接。SELinux 沒標之前，system_server 讀不到 extcon：

```text
ExtconUEventObserver: Unable to read .../1-0041/extcon/extcon0/cable.0/name.
This probably means the selinux policies need to be changed.
java.io.FileNotFoundException: ... open failed: EACCES (Permission denied)
```

標好之後，Android 的 media 播放會走 `AUDIO_DEVICE_OUT_HDMI`，螢幕有聲音。但這時候耳麥插上去，Android 仍然只知道 HDMI：

| 情況 | 加 HDMI 音訊之前 | 加 HDMI 音訊之後 |
|---|---|---|
| 接 HDMI | 全部走耳麥孔 | 全部走 HDMI，耳麥沒聲音 |
| 沒接 HDMI | 耳麥孔 | 耳麥孔 |
| 接 HDMI 又插耳麥 | 耳麥孔 | **仍然是 HDMI** |

### 3.2 codec 有偵測，但沒有往上報

Android 和 Yocto 的 dts 都沒有在音效卡節點設 `hp-det-gpios`，所以 fsl-asoc-card 沒有建立 jack。插拔時讀 codec 的 JACK_STATUS（暫存器 0x02）和 codec 中斷次數：

```bash
grep "^02:" /sys/kernel/debug/regmap/4-0010/registers
grep max98090 /proc/interrupts
dumpsys audio | grep -A3 "Connected devices"
```

| | 耳麥插著 | 耳麥拔掉 |
|---|---|---|
| JACK_STATUS（0x02） | `0x02` | `0x00` |
| max98090 中斷次數 | 1 | 2 |
| Android Connected devices | 只有 hdmi | 只有 hdmi |

codec 的 JDET 有動作。Kernel 裡有廠內 2022 年加的 codec IRQ 處理（兩筆 commit）：讀到 `JACK_STATUS == 0x02` 時，在 kernel 內切換 DMIC/AMIC 和喇叭路徑。**但沒有 ALSA jack control、沒有 input event、沒有 extcon，userspace 完全不知道。**

Yocto 也有兩張音效卡（`max98090-audio`、`sound-hdmi`），一樣有這個缺口。只是 Yocto 由應用程式或使用者指定輸出（`aplay -D`、default card、PulseAudio/PipeWire 的 default sink），不靠 kernel 的插拔狀態，所以沒有表現成 bug。Android 由 audio policy 依「已連接的裝置」自動選卡，沒有插拔回報就一直走 HDMI。

### 3.3 第一版：在 codec driver 加 extcon（已 abandon）

第一版直接在 max98090 driver 的 JDET IRQ 裡註冊 extcon，`JACK_STATUS == 0x02` 就報 HEADPHONE/MICROPHONE。板上 4 個步驟都通過（接 HDMI：沒插走 HDMI、插上切耳麥、拔掉回 HDMI、耳麥麥克風錄音正常）。

Review 提出三個問題：

| 來源 | 問題 |
|---|---|
| Review（blocking） | 只認 `0x02` 才算插入：3 極耳機讀到 `0x00` 會被當成拔出；耳麥按鍵按下時也讀到 `0x00`，extcon 會誤報拔出再插入 |
| Review（warning） | `CONFIG_EXTCON=n` 時 `devm_extcon_dev_allocate()` 回 `ERR_PTR(-ENOSYS)`，整顆 codec probe 失敗 |
| 主管 | 會影響其他 OS（Yocto）的 driver 修改要先在群組討論；改 driver 前先確認 upstream 是否已解、能否 backport、有沒有不大改 driver 的做法 |

### 3.4 查 upstream 和 NXP 既有機制

- **Upstream max98090**：2026-05 改成標準的 `set_jack` callback（`e2c428c3832e`、`b0ffc4b644de`），machine driver 呼叫 `snd_soc_component_set_jack()` 就能啟用 codec 原生的 `max98090_jack_work`，它會區分 headphone、headset、按鍵。Upstream 沒有在 codec 裡做 extcon。
- **NXP Android BSP**：`812b39e4c1eb`（"MA-12281 extcon: audio: support headset and hdmi hot-plug"）用 extcon 把插拔報給 Android：
  - HDMI：在 bridge driver 註冊 `EXTCON_DISP_HDMI`（dw-hdmi、cdns-hdmi、it6161；it6161 和 LT9611UXD 一樣是 DSI 轉 HDMI）。
  - 耳機：在 fsl-asoc-card，`hp-det-gpios` → `hp_jack_event` → extcon。**但 extcon 只對 wm8960/wm8962 配置**，max98090 不會有。
- **LT9611UXD**：mainline 沒有這顆 driver，沒有東西可以 backport。Mainline 的 `lontium-lt9611.c` 用 DRM bridge HDMI audio framework，插拔報成 ALSA jack；Android `WiredAccessoryManager` 的 HDMI 只讀 extcon，不吃 ALSA jack。

### 3.5 Backport upstream set_jack：這塊載板判斷錯誤

把 upstream 的 3 筆 set_jack 相關 commit backport 過來，fsl-asoc-card 對 max98090 建 headset jack 並呼叫 `snd_soc_component_set_jack()`，用 notifier 把狀態轉給 extcon。實測：

| 實際狀態 | JACK_STATUS | upstream jack_work 報的 | extcon |
|---|---|---|---|
| 沒插（開機） | `0x00` | Headphone | `HEADPHONE=1` ❌ |
| 插 4 極耳麥 | `0x02` | Headset | `HEADPHONE=1, MICROPHONE=1` ✅ |
| 拔掉 | `0x00` | Headset + 按鍵按下 | 停在 `HEADPHONE=1, MICROPHONE=1` ❌ |

原因在 upstream 的判斷表（`LSNS` = bit 2，`JKSNS` = bit 1）：

```c
switch (reg & (M98090_LSNS_MASK | M98090_JKSNS_MASK)) {
case M98090_LSNS_MASK | M98090_JKSNS_MASK:   /* 0x06 */
	/* No Headset Detected */
case 0:                                      /* 0x00 */
	if (max98090->jack_state == M98090_JACK_STATE_HEADSET)
		status |= SND_JACK_HEADSET | SND_JACK_BTN_0;  /* 按鍵 */
	else
		status |= SND_JACK_HEADPHONE;                 /* 耳機 */
case M98090_JKSNS_MASK:                      /* 0x02 */
	status |= SND_JACK_HEADSET;
}
```

Upstream 預期「沒插」讀到 `0x06`，這塊載板讀到的是 `0x00`。所以沒插被判成耳機；插過耳麥再拔掉，則被判成「耳麥按鍵按下」，永遠不會報拔出。這是載板接線和 codec 預設假設不同，backport 修不了。（另外，廠內 2022 年另一筆 commit 讓 JDWK 固定開啟，upstream jack_work 則會切換 JDWK，兩者也有衝突。）

### 3.6 找載板專用的偵測腳

載板線路圖上有一組獨立的耳機偵測電路：耳麥孔 pin 4 `HP_DET_C` 經 Q76 到 `Audio_HP_DET`（註記「H: w/ Headphone」），接到 SMARC 的 GPIO7/GPIO8 其中一支；另一支是 codec 中斷。改版記錄有一條「change Q76.G to Audio_HP_DET」，表示是硬體刻意加的。

dts 沒有用到這支腳，所以用 GPIO 快照比對找出對應的 SoC 腳：沒插時把 118 條可讀 GPIO 全部記下來，插上耳麥再記一次，只有一條改變：

| | GPIO4_IO01（`Audio_HP_DET`） | JACK_STATUS |
|---|---|---|
| 沒插 | 0 | `0x00` |
| 插 4 極耳麥 | 1 | `0x02` |

對應 SoM pad `ENET1_MDIO`，就在 codec 中斷 `GPIO4_IO00` 旁邊，電位和線路圖「H: w/ Headphone」一致。再用背景 logger 每秒讀 4 次確認插拔都跟得上：

```text
03:38:13  HP_DET=1  JACK=02   插著
03:41:06  HP_DET=0  JACK=00   拔掉
03:41:28  HP_DET=1  JACK=02   再插上
```

這是機械開關，3 極 / 4 極、耳麥按鍵都不會影響它。

### 3.7 改用 hp-det-gpios + NXP 既有 extcon 路徑

改法只剩兩筆 kernel 修改：dts 加 `hp-det-gpios`，fsl-asoc-card 的 extcon 配置條件加上 max98090（一行）。SELinux 的 genfs 路徑改到音效卡底下的 extcon。

開機後確認：

```bash
cat /sys/kernel/debug/gpio | grep -i "Headphone detection"
for e in /sys/class/extcon/*; do echo $e $(cat $e/name) $(cat $e/state); done
ls -Z /sys/devices/platform/sound-max98090/extcon
dumpsys media.audio_flinger | grep -E "Output device|Standby"
```

| 項目 | 結果 |
|---|---|
| 偵測 GPIO | kernel 認成 `Headphone detection`，插著讀 hi、拔掉讀 lo |
| extcon（`sound-max98090`） | 跟著 GPIO 變化，`HEADPHONE=1/0`，`MICROPHONE` 一直是 0 |
| SELinux | 節點是 `sysfs_extcon`，enforcing 下沒有 avc |
| 沒插 | 播放走 HDMI |
| 插上 | 切到 `AUDIO_DEVICE_OUT_WIRED_HEADPHONE`，耳機有聲音 |
| 拔掉 | 切回 HDMI |
| 耳麥麥克風 | 錄到講話 −13 ～ −20 dBFS，沒有 clipping |

只有 HEADPHONE，沒有 MICROPHONE：載板沒有獨立的麥克風偵測線。Android 把它當成「沒有麥克風的耳機」，但錄音走的還是 max98090 的 MIC1，實測錄音正常。

### 3.8 為什麼沒有 input event

評估過用 Android 的 `config_useDevInputEventForAudioJack=true` 改讀 input event（`SW_HEADPHONE_INSERT`），藉此不動 fsl-asoc-card。

- Framework 端可行：AOSP 16 的 `WiredAccessoryManager` 開了這個設定後，耳機狀態改由 input event 取得；如果同時有 extcon，HDMI 仍由 extcon 取得，只會印一行 "Both input event and extcon are used for audio jack, please just choose one"。
- Kernel 端不行：板上 `Headphone Jack` 這個 ALSA control 有（插著時是 `On`），但 `/proc/bus/input/devices` 只有 scmi、gpio-keys 和鍵盤，**沒有 jack 的 input device**。

原因是 fsl-asoc-card 在 `devm_snd_soc_register_card()` 回來之後才呼叫 `simple_util_init_jack()`。ALSA 的 jack input device 是在 `snd_card_register()` 時一起註冊，jack 晚於 card 註冊時，只會有 kcontrol，input device 永遠不會出現。NXP 的 Android EVK 走 extcon，所以沒踩到這個問題。

結論是改走 input event 反而要動更多 fsl-asoc-card 的程式碼，當時維持 extcon 做法。

### 3.9 開機時耳麥和 HDMI 都接著，播放走 HDMI

開機前耳麥已經插著、HDMI 也接著：kernel 兩個 extcon 都正確（`HEADPHONE=1`、`HDMI=1`，偵測 GPIO = hi），但 media 播放走 HDMI。開機後再拔插耳麥，切換都正常。

```text
WiredAccessoryManager: MSG_NEW_DEVICE_STATE   05:53:43.966   (extcon0，耳麥)
WiredAccessoryManager: MSG_NEW_DEVICE_STATE   05:53:43.967   (extcon1，HDMI)
```

`WiredAccessoryExtconObserver.init()` 開機時依 `/sys/class/extcon` 的順序讀初始狀態並逐一上報；這塊板子 extcon0 是耳麥、extcon1 是 HDMI，所以 HDMI 最後上報。Audio policy 對 media 選「最後接上的 removable device」，於是 HDMI 勝出。這不是 driver 偵測的問題。可行的改法另外整理在 §5.3。

### 3.10 Yocto 端跟進與 late_probe 移植

fsl-asoc-card 與 dts 兩筆推上 review 的隔天（11/20），Yocto kernel 由 Yocto 端跟進了四筆（中間的群組討論內容沒有留在本案的紀錄裡）：

| Yocto 端 commit | 內容 |
|---|---|
| revert 1 | Revert 廠內 2022 年的「speaker and headset switch」：板子沒有喇叭，切換沒有作用；插耳機時靜音喇叭改由 fsl-asoc-card 的 `hp_jack_event` 關 `Ext Spk` DAPM pin |
| revert 2 | Revert 廠內 2022 年的「detect api between amic and dmic」：板子沒有 DMIC；而且它在 DAPM 之外直接寫 DIGMICL/DIGMICR，會被下一次 DAPM 電源轉換蓋掉 |
| dts | dts 加 headphone detect GPIO（和 Android 端的 dts commit 內容相同） |
| late_probe | BACKPORT: ASoC: fsl-asoc-card: Register jacks in late_probe（取 upstream `de27e0cadcce` 的 jack 部分） |

Yocto 的 late_probe backport 正好就是 §3.8 的問題：把 jack 建立和 notifier 註冊搬到 `fsl_asoc_card_late_probe()`。`late_probe` 在 `snd_card_register()` 之前執行，input device 會跟著 card 一起註冊。另外加了 `fsl_asoc_card_free_jack()`，在 late_probe 失敗和 `card->remove` 時釋放 GPIO 與 notifier；card rebind 時 `snd_soc_jack_add_gpios()` 掛在 `card->dev` 的 devres 可能先跑掉並清掉 `gpio.jack`，所以只有 `gpio.jack` 還在時才 free，避免重複釋放。

移植到 Android 時和 Android 端的 extcon 一行修改有衝突（兩筆都改 fsl-asoc-card 的 jack 程式）。解法：

- jack 初始化照 Yocto 版移到 late_probe。
- Android 才有的 extcon 配置留在 `probe()`，但**移到 `devm_snd_soc_register_card()` 之前**。不移的話，late_probe 第一次 report jack 時 extcon 還沒建立，開機前就插著的耳麥不會回報給 Android。

板上驗證（開機時耳麥插著，HDMI 接著）：

```text
N: Name="max98090-audio Headphone Jack"
S: Sysfs=/devices/platform/sound-max98090/sound/card0/input1
H: Handlers=event1
/sys/class/extcon/extcon0 sound-max98090 MICROPHONE=0 HEADPHONE=1
/sys/class/extcon/extcon1 1-0041 HDMI=1
```

拔插 5 次，input event、extcon、Android 路由三者一致：

```bash
getevent -lt /dev/input/event1
```

```text
[ 606.424492] EV_SW  SW_HEADPHONE_INSERT  00000000
[ 612.628470] EV_SW  SW_HEADPHONE_INSERT  00000001
[ 620.412371] EV_SW  SW_HEADPHONE_INSERT  00000000
[ 627.000468] EV_SW  SW_HEADPHONE_INSERT  00000001
[ 632.168455] EV_SW  SW_HEADPHONE_INSERT  00000000
```

```text
08:42:40 HEADPHONE=0   MediaRouter: Audio routes updated: type=HDMI
08:42:46 HEADPHONE=1   MediaRouter: Audio routes updated: type=HEADPHONES
08:42:54 HEADPHONE=0   MediaRouter: Audio routes updated: type=HDMI
08:43:00 HEADPHONE=1   MediaRouter: Audio routes updated: type=HEADPHONES
08:43:06 HEADPHONE=0   MediaRouter: Audio routes updated: type=HDMI
```

Android 仍然用 extcon 判斷耳機，device/nxp 沒有打開 `config_useDevInputEventForAudioJack`。input event 是給 Yocto（PulseAudio/PipeWire、evtest）等其他 userspace 用的。

## 4. Root Cause 分析

### 4.1 背景與術語

| 術語 | 白話 |
|---|---|
| extcon | kernel 的「外部連接器」狀態框架，`/sys/class/extcon/extconN/state` 會列出 `HEADPHONE=1`、`HDMI=1` 等，變化時送 uevent |
| WiredAccessoryManager | Android system_server 裡管有線耳機 / HDMI / USB audio 的服務，預設讀 extcon，再通知 audio policy 哪些裝置已連接 |
| `config_useDevInputEventForAudioJack` | framework 設定，打開後耳機狀態改讀 input event（`SW_HEADPHONE_INSERT`） |
| ALSA jack | `snd_soc_jack`，會建立 `"Headphone Jack"` kcontrol，並在 card 註冊時一起註冊一個 input device |
| `hp-det-gpios` | fsl-asoc-card / simple-card 共用的 dts 屬性，描述一支「耳機插入」GPIO；driver 用它建 Headphone Jack |
| JACKSNS / JACK_STATUS | MAX98090 的耳機偵測腳和狀態暫存器（0x02），`LSNS` = bit 2，`JKSNS` = bit 1 |
| `late_probe` | ASoC card 的 callback，在所有 component 綁好後、`snd_card_register()` 之前呼叫 |
| MA-12281 | NXP Android BSP 的一筆修改（`812b39e4c1eb`），在 HDMI bridge driver 和 fsl-asoc-card 加 extcon，讓 Android 知道插拔 |

### 4.2 根因

```text
耳麥插入
  載板 Audio_HP_DET → GPIO4_IO01 = 1          ← dts 沒描述，沒人用
  codec JACKSNS → JACK_STATUS 0x00/0x02
    └─ codec IRQ：kernel 內切路徑               ← 不往上報
                                                  （upstream jack_work 又判錯）
  fsl-asoc-card
    ├─ 沒有 hp-det-gpios → 不建 Headphone Jack
    ├─ extcon 只對 wm8960/wm8962 配置           ← max98090 沒有 extcon
    └─ jack 在 register_card 之後才建           ← 有 kcontrol，沒有 input device
  Android WiredAccessoryManager
    └─ 沒有 extcon、沒有 input event → 只知道 HDMI → media 一直走 HDMI
```

事實整理：

1. **回報的缺口**：Android 依「已連接的裝置」自動選卡，必須有 extcon 或 input event。這塊板子 kernel 原本兩者都沒有。Yocto 也一樣沒有，只是使用方式讓它沒有表現成 bug。
2. **偵測來源選錯會誤判**：這塊載板的 codec JACKSNS 在沒插時讀 `0x00`，和 upstream 預期的 `0x06` 不同。codec 的判斷（不論廠內 2022 年的 `== 0x02` 或 upstream jack_work）在這塊板子上都無法正確區分「沒插 / 3 極耳機 / 按鍵」。載板硬體已經提供獨立的機械偵測 `Audio_HP_DET`，這才是正確的來源。
3. **NXP 既有機制只差設定**：fsl-asoc-card 已經支援 `hp-det-gpios` → `hp_jack_event` → extcon，只是 extcon 配置條件沒有 max98090、dts 沒有描述偵測腳。
4. **jack 註冊順序**：jack 在 `snd_card_register()` 之後才建立，input device 不會被註冊。upstream 已在 `de27e0cadcce` 把它移到 late_probe。

## 5. 解決方案

### 5.1 最終採用的組合

| Repo | 修改 | 內容 | 狀態 |
|---|---|---|---|
| kernel（Android） | dts | dts：`sound-max98090` 加 `hp-det-gpios = <&gpio4 1 GPIO_ACTIVE_HIGH>` 與 pinctrl | Merged |
| kernel（Android） | extcon 一行 | fsl-asoc-card：max98090 也配置 MA-12281 的 jack extcon（一行） | Merged |
| kernel（Android） | late_probe 移植 | Yocto 版移植：jack 移到 late_probe，extcon 配置移到 card 註冊之前 | Merged |
| device/nxp | genfscon | SELinux：`/sys/devices/platform/sound-max98090/extcon` 標成 `sysfs_extcon` | Merged |
| kernel（Android） | codec extcon | 在 codec driver 加 extcon（第一版） | Abandoned |
| lt9611uxd（共用） | HDMI extcon | HDMI extcon，只在 Android build（`LT9611UXD_ANDROID`）且 `CONFIG_EXTCON` 開啟時編入 | Merged |
| device/nxp | HDMI 音訊 | HDMI 音訊 json + audio policy | Merged |
| kernel（Yocto） | 2 筆 revert + dts + late_probe | 見 §3.10 | Merged |

codec driver（`max98090.c`）沒有新增任何程式碼。

### 5.2 Patch 摘錄

dts（Android 與 Yocto 相同）：

```diff
@@ sound-max98090 {
 		model = "max98090-audio";
 		audio-cpu = <&sai3>;
 		audio-codec = <&max98090>;
+		pinctrl-names = "default";
+		pinctrl-0 = <&pinctrl_hp_det>;
+		hp-det-gpios = <&gpio4 1 GPIO_ACTIVE_HIGH>;
 		audio-routing =
@@
+	pinctrl_hp_det: hpdetgrp {
+		fsl,pins = <
+			IMX95_PAD_ENET1_MDIO__GPIO4_IO_BIT1             0x31e
+		>;
+	};
```

fsl-asoc-card extcon 配置：

```diff
 #ifdef CONFIG_EXTCON
 	if (of_device_is_compatible(np, "fsl,imx-audio-wm8960") ||
-			of_device_is_compatible(np, "fsl,imx-audio-wm8962")) {
+			of_device_is_compatible(np, "fsl,imx-audio-wm8962") ||
+			of_device_is_compatible(np, "fsl,imx-audio-max98090")) {
 		fsl_asoc_card_edev = devm_extcon_dev_allocate(&pdev->dev,
 				fsl_asoc_card_cables);
```

狀態轉給 extcon 的是 NXP 既有的 `hp_jack_event()`（MA-12281）：

```c
	if (event & SND_JACK_HEADPHONE) {
#ifdef CONFIG_EXTCON
		if (fsl_asoc_card_edev)
			extcon_set_state_sync(fsl_asoc_card_edev, EXTCON_JACK_HEADPHONE, 1);
#endif
		/* Disable speaker if headphone is plugged in */
		return snd_soc_dapm_disable_pin(dapm, "Ext Spk");
	} else {
		...
```

late_probe 移植（Android 版）的重點：

```diff
 static int fsl_asoc_card_late_probe(struct snd_soc_card *card)
 {
 ...
+	ret = fsl_asoc_card_init_jack(card);
+	if (ret)
+		goto jack_fail;
 ...
+jack_fail:
+	fsl_asoc_card_free_jack(card);
+	return ret;
+}
+
+static int fsl_asoc_card_card_remove(struct snd_soc_card *card)
+{
+	fsl_asoc_card_free_jack(card);
+	return 0;
 }
 ...
 	priv->card.late_probe = fsl_asoc_card_late_probe;
+	priv->card.remove = fsl_asoc_card_card_remove;
 ...
-	ret = devm_snd_soc_register_card(&pdev->dev, &priv->card);
-	...
-	if (of_property_present(np, "hp-det-gpios") || ...) {
-		ret = simple_util_init_jack(&priv->card, &priv->hp_jack, ...);
-	...
 #ifdef CONFIG_EXTCON
 	if (... "fsl,imx-audio-max98090")) {
 		fsl_asoc_card_edev = devm_extcon_dev_allocate(...);
 	...
 #endif
+
+	/* Finish card registering */
+	platform_set_drvdata(pdev, priv);
+	snd_soc_card_set_drvdata(&priv->card, priv);
+
+	ret = devm_snd_soc_register_card(&pdev->dev, &priv->card);
```

SELinux：

```text
genfscon sysfs /devices/platform/sound-max98090/extcon u:object_r:sysfs_extcon:s0
```

HDMI 端：`lt9611uxd.mk`（只有 Android build 會 include）帶 `LT9611UXD_ANDROID=y`，Makefile 轉成 `ccflags-$(LT9611UXD_ANDROID) += -DLT9611UXD_ANDROID`；extcon 相關程式碼只在 `LT9611UXD_ANDROID` 且 `IS_ENABLED(CONFIG_EXTCON)` 時編入。不帶 macro 編出來的 `.ko` 沒有任何 extcon symbol，Yocto 行為不變。

### 5.3 考慮過但沒採用的做法

| 做法 | 問題 |
|---|---|
| 在 codec driver 的 IRQ 加 extcon（第一版） | 只認 `0x02`，3 極耳機和按鍵會誤判；`CONFIG_EXTCON=n` 時 codec probe 失敗；在共用 driver 加廠內程式碼，不符合「先找 upstream / 不大改 driver」的流程 |
| Backport upstream set_jack，用 codec 原生 jack_work | 這塊載板 JACKSNS 沒插讀 `0x00`：沒插報成耳機、拔掉報成按鍵（§3.5） |
| 只開 `config_useDevInputEventForAudioJack`，不動 fsl-asoc-card | 當時 jack 晚於 card 註冊，沒有 input device（§3.8）；要改的 driver 反而更多 |
| policy 把 `HDMI Out` 放進 `attachedDevices`（永遠連接） | 出問題後撤掉，改由 LT9611UXD extcon 動態連接 |
| 開機時耳麥優先：改 kernel probe 順序讓 HDMI extcon 先註冊 | 依賴 module 載入與 probe 時序，不穩定 |
| 開機時耳麥優先：vendor init 在開機後重新觸發一次耳麥狀態 | workaround |
| 開機時耳麥優先：frameworks/base `WiredAccessoryExtconObserver.init()` 改成先報 HDMI/LINE_OUT、後報耳機 | 約 10 行、結果固定，已在 extcon 一行修改的 review 中提出；截至撰寫時尚未決定是否實作 |

### 5.4 驗證結果

| 項目 | 方法 | 結果 |
|---|---|---|
| 偵測 GPIO 可靠性 | 每秒 4 次讀 `GPIO4_IO01` 和 JACK_STATUS | 插拔都正確跟隨 |
| Upstream set_jack backport | extcon + JACK_STATUS | 沒插誤報 HEADPHONE=1；拔掉停在 HEADSET（不採用） |
| hp-det-gpios + extcon | 接 HDMI，插拔耳麥 | 沒插 → HDMI；插上 → WIRED_HEADPHONE（耳機有聲音）；拔掉 → HDMI |
| 耳麥麥克風 | `tinycap` 30 秒 | 講話 −13 ～ −20 dBFS，沒有 clipping，回放清楚 |
| SELinux | enforcing | 沒有 avc |
| HDMI extcon（macro 版） | 開機 + 播放 | `extcon1` 1-0041 `HDMI=1`，HDMI 有聲音 |
| late_probe 移植（Android 版） | `/proc/bus/input/devices`、`getevent`、extcon、logcat | `Headphone Jack` 註冊成 input device；拔插 5 次 input event、extcon、Android 路由一致；開機前插著的耳麥也有正確回報到 extcon |
| Yocto 版 late_probe | 原作者在同一塊板上測 | SAI unbind/bind 後 jack GPIO 重新取得、input device 重建；card unbind 沒有 "Trying to free already-free IRQ" |

### 5.5 已知限制

- **只報 HEADPHONE，沒有 MICROPHONE**：載板沒有麥克風偵測線。Android 視為「沒有麥克風的耳機」，錄音仍然正常。
- **開機時耳麥和 HDMI 都接著**：11/19 實測播放走 HDMI（§3.9）。11/23 套上 late_probe 移植的版本，開機 log 裡 MediaRouter 報的是 `type=HEADPHONES`，但當時沒有用 `dumpsys media.audio_flinger` 確認實際輸出裝置，所以這個限制是否還在**尚未確認**。
- **3 極耳機、耳麥按鍵沒有實測**：測試用的耳麥沒有線控按鍵，也沒有 3 極耳機可用。偵測走機械開關，理論上不受影響，但沒有實際驗證。

## 6. 上游狀態

- **max98090 jack 偵測**：mainline 已改用標準 set_jack（`e2c428c3832e`、`b0ffc4b644de`），但其判斷假設 JACKSNS 沒插時為 `0x06`，不適用這塊載板，所以沒有採用。
- **fsl-asoc-card jack 註冊順序**：mainline `de27e0cadcce`（"ASoC: fsl-asoc-card: Move bound-component setup to late_probe"）已修。Yocto 端只取 jack 的部分（其餘依賴 deferrable card binding 的重構，這棵樹沒有），Android 再從 Yocto 版移植。
- **fsl-asoc-card extcon**：是 NXP Android BSP 自己的修改（MA-12281），不在 mainline。這次只是把 max98090 加進它的條件。
- **LT9611UXD**：mainline 沒有這顆 driver。HDMI extcon 是 Android 專用，用 macro 隔開。
- **Android 與 Yocto 的差異**：Yocto 已 revert 廠內 2022 年的兩筆 codec IRQ 切換。截至查詢時，Android kernel 上沒有對應的 revert，仍保留這兩筆。這塊板子沒有喇叭、沒有 DMIC，實測沒有看到影響，但兩邊行為不一致，後續要決定是否同步。

## 7. 結論與建議

1. **Android 的音訊路由完全依賴 kernel 回報插拔。** 只要有第二個輸出（HDMI、USB audio），耳機偵測就不能只在 kernel 內切路徑，一定要有 extcon 或 input event。Yocto 上「測項 PASS」不代表 Android 會正常。
2. **先找硬體設計好的偵測來源。** 這次 codec 的 JACKSNS 在這塊載板上語意不同，花了兩輪（自寫 extcon、backport upstream）才確定不能用。早點看線路圖，就會發現硬體已經有獨立的 `Audio_HP_DET`。找不到腳位時，可以用「插拔前後 GPIO 快照比對」快速定位。
3. **改 driver 前依序確認：upstream 是否已修 → 能否 backport → 有沒有不改 driver 的做法（dts、既有機制）。** 最後的方案在 codec driver 裡沒有新增程式碼，Android 和 Yocto 共用同一份 dts。
4. **共用 driver 的 Android 專用功能要隔開。** LT9611UXD 的 extcon 用 build macro 只在 Android 編入，Yocto 的 `.ko` 不變。
5. **ALSA jack 要在 card 註冊前建立。** 在 `devm_snd_soc_register_card()` 之後才建 jack，只會有 kcontrol，不會有 input device。檢查方式是看 `/proc/bus/input/devices` 有沒有 `... Headphone Jack`。
6. **待辦**：
   - 開機時耳麥優先的方案（frameworks/base 上報順序）待討論；先用 `dumpsys media.audio_flinger` 確認 late_probe 移植之後的開機路由。
   - 有 3 極耳機和線控耳麥時補測。
   - 決定 Android 是否跟進 Yocto 的兩筆 revert。
   - 程式碼觀察（未驗證）：fsl-asoc-card 的 extcon 配置失敗時直接 `goto asrc_fail`，沒有設定 `ret`，`fsl_asoc_card_edev` 也會留著 `ERR_PTR`（review 時有人提過）。late_probe 移植把這段移到 card 註冊之前後，配置失敗會連音效卡都不註冊。`CONFIG_EXTCON=y` 時正常不會走到這條路，但值得補上錯誤處理。

## 附錄

### A. 常用指令

```bash
# extcon 狀態（耳機、HDMI）
for e in /sys/class/extcon/*; do echo $e $(cat $e/name) $(cat $e/state); done
ls -Z /sys/devices/platform/sound-max98090/extcon

# 偵測 GPIO
grep -i "Headphone detection" /sys/kernel/debug/gpio

# codec JACK_STATUS
grep "^02:" /sys/kernel/debug/regmap/4-0010/registers

# ALSA jack 與 input device
tinymix -D 0 | grep -i jack
grep -B1 -A4 "Headphone Jack" /proc/bus/input/devices
getevent -lt /dev/input/event1

# Android 端
logcat -d | grep -E "WiredAccessoryManager|ExtconUEventObserver|Audio routes updated"
dumpsys audio | grep -A3 "Connected devices"
dumpsys media.audio_flinger | grep -E "Output device|Standby|Frames written"

# 錄音
tinycap /data/local/tmp/mic.wav -D 0 -d 0 -c 2 -r 48000 -b 16 -T 30
```

### B. 參考資料

- Linux：`sound/soc/fsl/fsl-asoc-card.c`（`hp_jack_event`、`fsl_asoc_card_late_probe`、`fsl_asoc_card_init_jack`、`fsl_asoc_card_free_jack`）
- Linux：`sound/soc/codecs/max98090.c`（`max98090_jack_work`）、`max98090.h`（`M98090_LSNS_MASK`、`M98090_JKSNS_MASK`）
- Linux commit `de27e0cadcce`：ASoC: fsl-asoc-card: Move bound-component setup to late_probe
- Linux commit `e2c428c3832e`、`b0ffc4b644de`：max98090 改用標準 set_jack callback
- NXP Android commit `812b39e4c1eb`：MA-12281 extcon: audio: support headset and hdmi hot-plug
- AOSP：`frameworks/base/services/core/java/com/android/server/WiredAccessoryManager.java`（`WiredAccessoryExtconObserver`、`config_useDevInputEventForAudioJack`）
- `device/nxp/imx9/<board>/sepolicy/genfs_contexts`（`sysfs_extcon`）
- 載板線路圖：耳麥孔 `HP_DET_C` → Q76 → `Audio_HP_DET`
