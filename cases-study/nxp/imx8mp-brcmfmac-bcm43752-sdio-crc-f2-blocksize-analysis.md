# i.MX8MP brcmfmac WiFi 高流量崩潰分析（BCM43752 F2 blocksize 512→256）

## 1. 問題概述

在 i.MX8MP 平台從 vendor bcmdhd driver 遷移到 mainline brcmfmac 後，WiFi 一跑 iperf 壓測就在數秒內崩潰：

```
$ iperf -c 192.168.137.223 -t 60 -i 1 -P4
[SUM]  0.0- 5.0 sec  1.75 MBytes   769 Kbits/sec
write failed: Connection reset by peer
```

dmesg 錯誤簽名（每次都相同）：

```
brcmfmac: mmc_submit_one: CMD53 sg block write failed -84
mmc0: Timeout waiting for hardware interrupt.
brcmfmac: brcmf_sdio_txfail: sdio error, abort command and terminate frame
brcmfmac: brcmf_sdio_dpc: failed backplane access over SDIO, halting operation
brcmfmac: brcmf_sdio_hdparse: seq NN: max tx seq number error   （之後連環出現）
```

主要症狀：

- iperf 起流量後 **1–4 秒**內吞吐量歸零、TCP 連線被 reset
- `-84` = `-EILSEQ`（資料 CRC 錯誤），發生在 SDIO 匯流排層
- driver 判定 backplane 存取失敗後 **halt**，wlan0 整個死亡，需等 driver 恢復或重連
- idle / 輕量流量正常，**只有持續高流量觸發**，100% 重現
- 換測試工具（iperf2/iperf3）、換並行數（單條 / `-P4` / `-P8`）都一樣崩
- 同一片板子跑**舊 BSP（bcmdhd driver）完全正常**

## 2. 系統環境

| 項目 | 版本 / 型號 |
|---|---|
| 平台 | i.MX8MP（客製載板） |
| 新 BSP（會崩） | Yocto walnascar，linux-imx **6.12.34**，**brcmfmac**（mainline） |
| 舊 BSP（正常） | linux-imx 6.6.23，**bcmdhd**（vendor out-of-tree） |
| WiFi 模組 | AMPAK AP6275S（**BCM43752**，SDIO） |
| SDIO host | usdhc1（sdhci-esdhc-imx），**SDR104 @ 200MHz**，1.8V，4-bit |
| 測試對端 | Windows 11 行動熱點（5GHz），iperf 2.0.9 / iperf3 3.21 |

兩版 BSP 的 device tree `&usdhc1` 電氣設定**完全相同**（pinctrl pad 0x196/0x1d6、`vqmmc-1-8-v`、無 `max-frequency` 上限），runtime 皆協商到 SDR104 / actual clock 200MHz（`/sys/kernel/debug/mmc0/ios`）。

## 3. 除錯過程

### 3.1 排除環境因素

| 假設 | 驗證 | 結果 |
|---|---|---|
| 過熱 | thermal_zone 讀值 | 32–34°C，排除 |
| 電源 brownout / 重開機 | `/proc/uptime`、journal boot ID | uptime 連續、無重開，排除 |
| RF 訊號 | `iw dev wlan0 station dump` | -28～-38 dBm、tx bitrate 1200Mbit/s，極佳，排除 |
| 測試工具差異 | iperf2 原生指令 vs iperf3 | 同樣崩潰、同樣 dmesg 簽名，排除 |
| WiFi 省電 | `iw dev wlan0 set power_save off` | 崩潰前撐得稍久（1s→4s），但仍崩，非主因 |

### 3.2 Workaround 嘗試：SDIO 降速

在 DT `&usdhc1` 加 `max-frequency = <100000000>`（SDR104 → 100MHz）後穩定，
但 TCP 吞吐量僅 ~165 Mbps。此時的（錯誤）推論是 200MHz 訊號完整性極限 ——
與 NXP EVK 參考 DT 比對後發現 pad drive strength / tuning 參數完全一致且 DSE 已是最大檔，
表面上支持 SI 理論。

### 3.3 關鍵對照實驗：舊 BSP（bcmdhd）同時脈壓測

燒回舊 BSP 確認 bcmdhd 的實際 SDIO 狀態：

```
[dhd] sdioh_attach: sd clock rate = 208000000
mmc0: new ultra high speed SDR104 SDIO card at address 0001
/sys/kernel/debug/mmc0/ios → actual clock 200000000 Hz, timing spec: SDR104
```

**bcmdhd 同樣跑在 SDR104 / 200MHz**，同一條 `iperf -P4 -t60`：

```
[SUM]  0.0-60.0 sec  1.98 GBytes   284 Mbits/sec     ← 跑滿 60 秒，dmesg 0 錯誤
```

→ **「硬體 200MHz 極限」理論被推翻**。同硬體、同模組、同時脈，差異只剩 driver 堆疊。

### 3.4 Driver 差異分析（bcmdhd vs brcmfmac）

比對兩個 driver 的 SDIO 傳輸路徑，找到兩個結構性差異：

| 差異點 | brcmfmac（崩） | bcmdhd（穩） |
|---|---|---|
| 資料傳輸方式 | **SG 鏈式 CMD53**（TX glom 把多個封包串成 scatterlist 一次寫入；RX glom superframe 鏈式讀取） | 每封包**單一緩衝 CMD53**，不用 host SG |
| **F2 blocksize** | 43752 不在特例名單 → 預設 **512**（`bcmsdh.c`） | 明確設 **256**（開機 log：`set sd_f2_blocksize 256`） |

錯誤訊息中的 `sg` 字樣（`CMD53 sg block write failed`）直接對應第一個差異。

### 3.5 單一變因隔離實驗矩陣

全部在 SDR104 / 200MHz 下，以 `iperf -P4` 壓測 + iperf3 雙向各 5 分鐘 soak 驗證：

| # | 配置 | SG/glom | F2 blksz | 結果 |
|---|---|---|---|---|
| 1 | brcmfmac 預設 | 開 | 512 | ❌ 1–4 秒致命崩潰（CMD53 **write** -84 → backplane halt） |
| 2 | `brcmfmac.txglomsz=1`（module param，TX 不用 SG） | RX glom 仍開 | 512 | ⚠️ 跑完不 halt，但 **CMD53 sg *read* failed -84** 約 14 筆/分（可恢復） |
| 3 | `sg_support = false`（SG 全關） | 關 | 512 | ❌ 乾淨跑 3 分鐘後硬掛：`mmc0` ADMA Err → `RXHEADER FAILED: -110` → **晶片韌體 `PSM's watchdog has fired!`** |
| 4 | `sg_support = false` + blksz **256** | 關 | 256 | ✅ 雙向 10 分鐘 **0 錯誤**（= bcmdhd 傳輸剖面），但 TX 僅 142 Mbps |
| 5 | **只改 blksz 256**（SG/glom 全開） | 開 | 256 | ✅ **雙向 10 分鐘 0 錯誤，RX 265 / TX 273 Mbps** |

實驗 #2 vs #3 的對比說明 SG 只是放大器而非根因（SG 全關反而換一種方式掛）；
實驗 #5 證明 **blocksize 才是唯一必要的修正**。

## 4. Root Cause 分析

### 4.1 背景：SDIO 傳輸的三個層次（glom / SG / F2 blocksize）

先解釋幾個後文會用到的名詞：

| 名詞 | 白話意思 |
|---|---|
| skb | Linux 內核中的「一個網路封包」。每個約 1.5KB，各自佔一塊獨立的記憶體，彼此位置不相鄰 |
| CMD53 | SDIO 的「大量資料讀/寫」指令，WiFi 封包都靠它進出晶片 |
| DMA / ADMA | 幫 CPU 搬資料的硬體引擎；ADMA 是進階版，能照一張「地址+長度」清單自動連續搬多段資料 |
| function（F0/F1/F2） | 一張 SDIO 卡內部的多個邏輯通道，像同一條匯流排上的不同「埠」。F0 固定是標準控制暫存器；WiFi 晶片慣例用 F1 存取晶片內部暫存器、**F2 走 WLAN 資料封包**（資料主幹道） |

一筆 TX 封包從系統送到晶片的旅程，三個關鍵機制各在不同層：

```
[多個 skb 封包]（各自獨立、位置不相鄰的記憶體緩衝區）
   ↓ ① glom（聚合）—— 協定層
[superframe：把 N 個封包串成一批，用一次 CMD53 送出]
   ↓ ② SG（scatter-gather）—— 記憶體/DMA 層
[scatterlist：不複製資料，列一張「地址+長度」清單交給 ADMA 照著抓]
   ↓
[uSDHC 控制器（ADMA）依清單逐段抓資料]
   ↓ ③ CMD53 block mode —— SDIO 匯流排層
[資料切成固定大小的 block（512B 或 256B）逐一傳輸，每個 block 各帶 CRC16 校驗]
   ↓
[BCM43752 的 F2 通道接收]
```

**① glom（glomming，聚合）**：每次 CMD53 都有固定開銷（命令、握手、中斷），
小封包逐一傳會被開銷吃掉頻寬，所以 brcmfmac 把多個封包聚合成 superframe 一次傳。
TX 方向為 txglom；RX 方向由韌體在晶片端打包、host 一次讀回（rxglom）。
`txglomsz=1` 即「一批最多 1 個封包」＝實質關閉 TX 聚合。

**② SG（scatter-gather）**：聚合的封包在記憶體中不連續，要一次傳出去有兩種做法。
bcmdhd 用 bounce buffer：先把封包逐一**複製**到一塊連續的大緩衝再傳（簡單、多耗一次
memcpy）；brcmfmac 用 SG：列清單讓 ADMA 逐段抓（省 CPU，但清單每段的長度是
封包大小，不是 block 的整數倍，形狀不規則）。
`sg_support=false` 即退回單一緩衝傳輸（rxglom 也隨之關閉）。

**③ F2 blocksize**：CMD53 傳大量資料時用 block mode —— 資料切成固定大小的
block 逐一傳，**每個 block 尾端各帶 CRC16 校驗**。此值在 probe 時由
`sdio_set_block_size()` 對 F2 通道設定：

```
blocksize=512:  [512B資料+CRC] [512B資料+CRC] ...
blocksize=256:  [256B+CRC] [256B+CRC] [256B+CRC] ...
```

block 是 CRC 校驗與晶片 FIFO 處理的最小單位：SDR104（200MHz）下，晶片 F2
必須以線速**連續吞完一整個 block 不能斷氣**，中途斷氣資料即損毀，host 端
表現為 data CRC 錯誤（-EILSEQ / -84）。

三個實驗旋鈕與層次的對應：

| 實驗旋鈕 | 動的層次 | 效果 |
|---|---|---|
| `txglomsz=1` | ① 關 TX 聚合 | 寫入變小批，較少觸發，但 RX 聚合仍在 → 零星 read CRC |
| `sg_support=false` | ② 關散聚清單 | 單一緩衝傳輸，但 block 仍是 512 → 換一種方式掛 |
| `f2_blksz=256` | ③ 改匯流排切塊 | **對症下藥**：晶片 F2 吞得下，①② 全開也 0 錯誤 |

一句話：glom 決定「一次送幾個封包」、SG 決定「host 怎麼餵資料給控制器」、
F2 blocksize 決定「匯流排上每一口切多大」。病根在第三層（晶片吞不下 512 的口），
前兩層只影響「餵得多猛」—— 餵越猛越快嗆到，所以初期看起來像 SG/glom 的錯。

### 4.2 根因：F2 blocksize 512 超出 BCM43752 的承受能力

**brcmfmac 對 BCM43752 使用了它無法穩定承受的 F2 blocksize 512。**

`drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c` 的 `brcmf_sdiod_probe()`：

```c
#define SDIO_FUNC2_BLOCKSIZE		512
#define SDIO_435X_FUNC2_BLOCKSIZE	256

	switch (sdiodev->func2->device) {
	case SDIO_DEVICE_ID_BROADCOM_CYPRESS_4373:
		f2_blksz = SDIO_4373_FUNC2_BLOCKSIZE;
		break;
	case SDIO_DEVICE_ID_BROADCOM_4359:
	case SDIO_DEVICE_ID_BROADCOM_4354:
	case SDIO_DEVICE_ID_BROADCOM_4356:
		f2_blksz = SDIO_435X_FUNC2_BLOCKSIZE;
		break;
	...
	default:
		break;			/* ← 43752 落到這裡，用 512 */
	}
```

關鍵事實：

1. **上游本來就有多顆晶片被特例設為 256**（4373 用 `SDIO_4373_FUNC2_BLOCKSIZE`、
   4359/4354/4356 用 `SDIO_435X_FUNC2_BLOCKSIZE`，兩者皆為 256）。追 git 歷史發現：
   commit `d2587c57ffd8`（"brcmfmac: add 43752 SDIO ids and initialization"）加入
   43752 支援時，**F2 watermark 是比照 4373 處理的，卻漏了 4373 同樣有的
   blocksize 256 限制** —— 這就是缺口的由來。
2. **vendor bcmdhd 對此晶片一直用 256**（`sd_f2_blocksize=256`），這就是舊 BSP 十年穩定的原因。
3. 512-byte block 在 SDR104 持續傳輸下產生 data phase CRC 錯誤（-EILSEQ），
   依傳輸模式不同呈現為：SG 寫入 CRC 風暴（致命）、SG 讀取零星 CRC（可恢復）、
   或單一緩衝下最終把晶片韌體打到 PSM watchdog reset。
   SG 鏈式傳輸是**放大器**（讓故障更快更猛），不是根因。
4. 100MHz 降速 workaround 之所以有效，只是把 512-block 的時序邊際「藏」回容忍範圍內，
   代價是頻寬砍半 —— 修正 blocksize 後完全不需要降速。

## 5. 解決方案

### 5.1 修正 patch（一行）

上游正式版（比照 4373 分組，對應 `d2587c57ffd8` 當初的 F2 watermark 處理方式）：

```diff
--- a/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c
+++ b/drivers/net/wireless/broadcom/brcm80211/brcmfmac/bcmsdh.c
@@ -911,6 +911,7 @@ int brcmf_sdiod_probe(struct brcmf_sdio_dev *sdiodev)
 	switch (sdiodev->func2->device) {
+	case SDIO_DEVICE_ID_BROADCOM_43752:
 	case SDIO_DEVICE_ID_BROADCOM_CYPRESS_4373:
 		f2_blksz = SDIO_4373_FUNC2_BLOCKSIZE;
 		break;
```

命名注意：43752 的 SDIO device id 在 v6.18 由 commit `74e2ef72bd4b`
（"wifi: brcmfmac: fix 43752 SDIO FWVID incorrectly labelled as Cypress (CYW)"）
從 `SDIO_DEVICE_ID_BROADCOM_CYPRESS_43752` 改名為 `SDIO_DEVICE_ID_BROADCOM_43752`。
**v6.17 以前的 stable / BSP tree（含本案 6.12）需用舊名 `CYPRESS_43752`** 才能編譯，
或連同 rename patch 一起 cherry-pick。上游 patch 帶
`Fixes: d2587c57ffd8 ("brcmfmac: add 43752 SDIO ids and initialization")`。

同時**移除**調查過程中的所有 workaround：DT 的 `max-frequency`、
`brcmfmac.txglomsz=1`、`sg_support=false` —— 全部不再需要。

### 5.2 Yocto 整合

```
meta-<layer>/recipes-kernel/linux/
├── linux-imx_%.bbappend
└── linux-imx/0001-brcmfmac-use-256B-F2-blocksize-for-BCM43752.patch
```

### 5.3 驗證結果（SDR104 / 200MHz，iperf3 -P4 各方向 5 分鐘）

| 測試 | 修正前（blksz 512） | 修正後（blksz 256） |
|---|---|---|
| `iperf -P4 -t60` 壓測 | 1–4 秒崩潰，WLAN halt | ✔ 跑滿，穩定 |
| 板子 RX 5 分鐘 soak | 無法完成 | ✔ **265 Mbps**（9.24 GB），0 錯誤 |
| 板子 TX 5 分鐘 soak | 無法完成 | ✔ **273 Mbps**（9.53 GB），0 錯誤 |
| dmesg SDIO/韌體錯誤 | CRC 風暴 → halt / PSM watchdog | ✔ **0** |
| 對照組 bcmdhd（284 Mbps） | — | 吞吐量差距 <5% |

（絕對數值受 Windows 熱點對端限制，重點為修正前後與 bcmdhd 對照組的相對比較。）

### 5.4 補充：STA / AP 兩種拓撲實測（修正版）

以修正版（blocksize 256）分別在兩種角色下實測（iperf3 `-P4` 各方向 60 秒）：

| 拓撲 | 頻寬 | PHY rate 上限 | 板子 RX | 板子 TX | 瓶頸 |
|------|------|--------------|---------|---------|------|
| 板子 = STA（對端 Windows 熱點） | 80 MHz | ~1200 Mbit/s（HE80 2SS） | **291 Mbps** | **299 Mbps** | **SDIO 介面**（4-bit 200MHz 實務上限 ~300–400） |
| 板子 = AP（connman tethering，單一 client） | **20 MHz** | ~172–287 Mbit/s（HE20 2SS） | 117 Mbps | 112 Mbps | **無線 20 MHz 頻寬** |

兩種模式雙向持續負載下 **SDIO 均零新增錯誤**，修正在 STA 與 AP 路徑皆穩定。

觀察與註記：

1. STA 模式的 ~290 Mbps 已達 **SDIO 介面版模組的實務天花板**
   （4-bit @ 200MHz raw 800 Mbit/s，扣除 CMD53/block CRC/glom/協定開銷後約 300–400），
   且雙向對稱、與 bcmdhd 的 284 Mbps 同級 —— 修正後的 brcmfmac 沒有把性能留在桌上。
   要突破此天花板需改用 PCIe 介面的模組。
2. AP 模式偏低純為無線設定：**connman tethering 經 wpa_supplicant 起 AP 時只帶
   `frequency`，未帶 `ht40` / `vht` / `max_oper_chwidth`，故固定 20 MHz**。
   PHY 上限 172–287 Mbit/s 下實測 112–117 Mbps，效率正常。
   如需 AP 模式高吞吐，須 patch connman 補上 VHT80 參數（或改用 hostapd），
   預期可回到 SDIO 天花板同級（~290 Mbps）。
3. 判斷頻寬的技巧：`iw dev wlan0 link` 的協商 bitrate 可反推 channel width ——
   HE 2SS 各頻寬頂速為 20MHz=287.5 / 40MHz=573.5 / 80MHz=1201 Mbit/s，
   實測值落在哪張速率表即為該頻寬。

## 6. 上游狀態

- Patch 已投稿 linux-wireless（2026-07-13）：
  **[PATCH] wifi: brcmfmac: set F2 blocksize to 256 for BCM43752**
  <https://lore.kernel.org/all/20260713-b43752-f2-blksz-v1-1-8697fcfeaef4@gmail.com/>
- brcmfmac maintainer（Arend van Spriel）回覆 **"Looks good to me"**，
  僅對 stable 標註方式提出疑問；Infineon（Gokul Sivakumar）補充 id rename 歷史
  （`74e2ef72bd4b`，v6.18 併入），並建議 stable 樹將 rename patch 與本 patch
  一併 cherry-pick。後續視需要送 v2 修正 stable 標註格式
  （prerequisite 寫法，邊界更正為 <= 6.17）。
- 同類公開回報（症狀相同、平台不同）：
  - Infineon 社群：i.MX7 + CYW43455 `CMD53 sg block read failed -84`
    （<https://community.infineon.com/t5/Wi-Fi-Bluetooth-for-Linux/multiple-instances-of-brcmf-sdiod-sglist-rw-CMD53-sg-block-read-failed-84-on/td-p/227451>）
  - TI AM335x/AM437x：需設 `sd_sgentry_align=512` workaround
    （<https://e2e.ti.com/support/processors-group/processors/f/processors-forum/541016/am335x-error-message-in-sdio-driver>）
  - 這些平台的 SG 對齊 workaround 可能同樣只是在緩解「blocksize 不合該晶片」的放大效應，值得重驗。

## 7. 結論與建議

1. **vendor driver → mainline driver 遷移時，「driver 換了」往往伴隨一整組隱性參數改變**
   （本例：F2 blocksize 512↔256、host SG 鏈式傳輸有↔無）。遷移驗證必須包含持續高流量壓測，
   不能只驗連線與 idle；新增 SDIO WiFi 晶片支援時，F2 blocksize 應向 vendor driver /
   晶片家族既有特例對齊，而非默認 512。
2. **同硬體 A/B 對照（vendor vs mainline driver）是最高價值的實驗**：
   本案一度誤判為硬體 SI 極限（pad/tuning 比對支持該理論），
   是 bcmdhd 同時脈穩跑 284 Mbps 的實測一槌定音推翻了它。
