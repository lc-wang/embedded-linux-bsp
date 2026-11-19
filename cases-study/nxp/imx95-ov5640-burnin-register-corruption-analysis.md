# i.MX95 OV5640 burn-in 中 camera 卡住分析（sensor 暫存器在 stream-on 時被改壞）

## 1. 問題概述

一片 i.MX95 SMARC 自製板在 Yocto 上跑 burn-in（多項測試同時反覆執行）時，camera 測項跑一段時間後就開始每次失敗：`gst-launch-1.0 libcamerasrc` 拍照逾時，之後每一輪都失敗，直到 burn-in 停下來、sensor 斷電後才自己恢復。

已知的現象：

- 單獨跑 camera 測項不會發生，要和 burn-in 的其他測項一起跑才會。
- 發生時間差異很大，從開跑 30 秒到 3.5 小時都有，平均約 35 分鐘一次。
- 卡住時 dmesg 沒有任何 camera、CSI、ISI 或 I2C 的錯誤。
- 換 camera cable、換另一整組板子（carrier、SOM、camera 模組、cable 全換）都會發生。

一開始懷疑 MIPI CSI 或 ISI driver 卡死。實際上是 **OV5640 自己的暫存器被改成亂數**（例如 0x3004 變成 0x00，MIPI 就停在 LP-11 不再送影像）。這些寫入不是 Linux 送的，而是在 sensor 開始出圖（stream-on）後 0.05–0.5 秒內，sensor 在 I2C 上「多看到」了寫入。最可能是硬體層面的耦合（I2C 線或電源受干擾），需要示波器量測才能確認。

考量量產時程，硬體短期不改，改用測試手法避開：burn-in 的 camera 測項從「每輪重新啟動拍照」改為「持續串流 + 定時檢查畫面」。同條件 3 小時驗證，0 次失敗。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95 |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP Yocto，kernel 6.18（linux-imx） |
| Camera | OV5640，MIPI CSI-2 2 lane |
| 影像路徑 | OV5640 → `dw-mipi-csi2`（`4ad30000`）→ ISI（`4ad50000`），使用 libcamera + `libcamerasrc` |
| Sensor 控制 | I2C 位址 `0x3c`，Linux `i2c-2` = LPI2C3（`42530000`），預設 400 kHz，有 eDMA |
| 同一條 I2C 上的其他裝置 | PCAL6408 GPIO expander（`0x20`，DIDO 輸出）、SOM EEPROM（`0x50`） |
| I2C 路徑 | SoC 1.8 V → SMARC `I2C_CAM1` → 載板自動方向電壓轉換器（SGM4574，1.8 V ↔ 3.3 V，2.2 kΩ 上拉）→ camera FPC |
| Burn-in | 自製 test-framework，約 36 項測試以 coproc 方式同時反覆執行，camera 測項每 4–5 秒重新啟動 pipeline 拍一次照 |

## 3. 除錯過程

### 3.1 卡住時的狀態

卡住時 `libcamerasrc` 停在 poll，ISI 的中斷計數不再增加。看 CSI 控制器的 PHY 狀態和 sensor 暫存器：

| 項目 | 正常出圖 | 卡住 |
|---|---|---|
| CSI `PHY_STOPSTATE` | lane 在 HS 和 LP-11 之間切換 | 100% `0x00010003`（clock + 2 lane 全在 LP-11） |
| Sensor runtime PM | active / suspended 交替 | 一直 active |
| OV5640 `0x3008`（system control） | `0x02` | 依情況不同 |
| OV5640 `0x3001`/`0x3002`/`0x3004` | `0x08`/`0x1c`/`0xff` | 第一次抓到時三個都是 `0x00` |

`0x3000`–`0x3004` 是 OV5640 各模組的 reset 和 clock enable。手動把 `0x3004` 寫成 `0x00`，就能完全重現：MIPI 停在 LP-11、ISI 沒有中斷、狀態一直持續。把 `0xff` 寫回去立刻恢復。

這也解釋了「burn-in 停下來才恢復」：burn-in 時 camera 每 4–5 秒就重新啟動一次，sensor 的 runtime PM autosuspend 是 1000 ms，所以 sensor 一直沒斷電。只有在 burn-in 停下來後，sensor 真正斷電，driver 下次上電時重寫整份初始化暫存器表，才把被改壞的值蓋回來。

另外確認了 OV5640 上電預設值（`0x3001=0x08`、`0x3004=0xcf` 等），被改的值不是 power-on reset 的結果。

### 3.2 用暫存器檢查抓發生時間

寫一個檢查程式，在 burn-in 進行中每隔幾百毫秒讀一次 sensor 的約 280 個暫存器（涵蓋 `0x3000`–`0x583f`），和開始時的快照比對，一有差異就記下時間和內容。

| 時間 | 條件 | 被改的暫存器 |
|---|---|---|
| 第 1 次 | 400 kHz，DMA 開 | 約 150 個暫存器變成小亂數 |
| 第 2 次 | 同上 | `0x3000`–`0x3008` = `91 e4 9d 9c 72 9c 9c 72 69`（連續 9 byte，`0x3008=0x69` 讓 sensor 進入 power down） |
| 第 3 次 | 除錯 kernel，記錄所有 I2C 傳輸 | `0x3600`、`0x3700`、`0x4400` → `0x00` |
| 第 4 次 | 除錯 kernel，記錄 lpi2c 暫存器 | `0x3000`–`0x3004` = `5d 65 67 63 59` |

有個規律：被改的位址都是 **sensor 當下的位址指標**，也就是上一筆傳輸剛寫入的起始位址。第 3 次的 `0x3600`/`0x3700`/`0x4400` 正好是檢查程式讀取區塊的起始位址。

### 3.3 Linux 有沒有送這些寫入

kernel 沒有 ftrace，所以用除錯 kernel 直接在 `i2c-2` 上記錄所有送給 `0x3c` 的傳輸（長度、內容、呼叫堆疊），另一版在 `lpi2c` driver 裡記錄每筆傳輸的 `MSR`/`MFSR`/`MDER` 狀態，放到 debugfs。

結果：

- 被改的那段時間，**Linux 沒有送任何寫到那些位址的傳輸**。
- 那一筆傳輸的 lpi2c 狀態暫存器、傳輸時間都和正常的一模一樣，driver 看不到任何異常。
- 有完整記錄的四次，都發生在 sensor 開始出圖（寫 `0x4202=0x00`、`0x3008=0x02`）後 0.05–0.5 秒內。

### 3.4 排除可能的原因

| # | 實驗 | 結果 | 結論 |
|---|---|---|---|
| 1 | 換 camera cable | 40 分鐘發生 | cable 不是原因 |
| 2 | 關掉 I2C DMA（U-Boot `fdt rm` lpi2c3 的 `dmas`，確認整輪 0 次 DMA 傳輸） | 40 分鐘發生 | DMA 不是原因 |
| 3 | I2C 降到 100 kHz（U-Boot `fdt set clock-frequency`） | 30 秒就發生 | 降速反而更容易發生，可當快速重現方法 |
| 4 | 100 kHz，只跑 camera 拍照 + 暫存器檢查 | 15 分鐘、195 次拍照都沒發生 | 需要 burn-in 其他測項參與 |
| 5 | 換另一整組板子（SD 卡開機，正式 image） | 約 5 分鐘發生 | 不是單片問題，是設計或系統層面 |
| 6 | 同一條 I2C 上的 DIDO、EEPROM 單獨或一起跑 | 10–15 分鐘都沒發生 | 同匯流排的其他裝置不是主因 |
| 7 | 只跑 camera 測項（含每次先跑 `cam -l` 讓 sensor 快速開關電） | 10 分鐘、402 次都沒發生 | 快速開關電不是原因 |

U-Boot 的設定只對當次開機有效，不需要改板上的 DTB：

```text
run mmcargs; run loadimage; run loadfdt; run loadoverlay
fdt addr ${fdt_addr_r}
fdt rm /soc/bus@42000000/i2c@42530000 dmas
fdt rm /soc/bus@42000000/i2c@42530000 dma-names
fdt set /soc/bus@42000000/i2c@42530000 clock-frequency <100000>
run boot_os
```

降速反而更容易發生，可以這樣理解：100 kHz 下每筆傳輸變成約 4 倍長，落在 stream-on 後那段受干擾時間內的機會就變大。

### 3.5 用測項組合統計找觸發條件

發生時間從 30 秒到 40 分鐘都有，單輪的有或沒有很容易誤判。所以寫了一個排程，在板上跑真正的 burn-in，把測項分成 9 組，用 21 種組合輪流跑，每種一次 1 小時、每輪順序打亂，共 62 小時、3 輪：

- `ALL`：全部測項
- `NO_G1`～`NO_G9`：全部但拿掉一組
- `CAM_G1`～`CAM_G9`：只有 camera 加一組
- `CAM`：只有 camera
- `ETH0DOWN`：全部，但 eth0 拔線

發生時自動記錄、讀出 sensor 暫存器、停掉 burn-in 讓 sensor 斷電恢復，再以同條件繼續。

| 組合 | 分鐘 | 事件 | 平均幾分鐘一次 |
|---|---|---|---|
| ALL | 171 | 5 | 34 |
| NO_G1（拿掉 CPU/GPU/VPU/NPU） | 174 | 3 | 58 |
| NO_G2（拿掉 mem/eMMC/microSD） | 169 | 8 | 21 |
| NO_G4（拿掉 DIDO + EEPROM） | 173 | 4 | 43 |
| NO_G7（拿掉 COM/CAN） | 168 | 8 | 21 |
| **NO_G9（拿掉 ethernet）** | 177 | **0** | — |
| **ETH0DOWN（eth0 拔線）** | 177 | **0** | — |
| **CAM_G9（只有 camera + ethernet）** | 177 | **0** | — |
| CAM、CAM_G1～G8 | 各 168～177 | 0 | — |

（只列出部分組合；有出事的 9 種組合合計 1489 分鐘、42 次，約 35 分鐘一次。）

依 35 分鐘一次的頻率，177 分鐘內 0 次的機率約 0.7%，所以這幾個 0 次不太可能是運氣：

- 要「ethernet 測項打 eth0」，而且「還有其他測項同時在跑」才會發生。
- 只有 camera + eth0 大流量也不夠。之後另外用 iperf3 雙向各約 92 Mbit/s 跑 89 分鐘，camera 3885 次全部成功。
- 拿掉 G1～G8 任一組都還會發生，看不出另外哪一組是必要的，比較像是整體負載或電源變動。
- 之後把 ethernet 測項改走 eth1，146 分鐘仍發生 2 次，所以也不只跟 eth0 的流量有關。

### 3.6 硬體路徑

對照線路圖和板子擺位：

- 乙太網路 PHY 和 camera 沒有共用穩壓器，但整顆 SOM 的電流都來自載板上同一顆 12 V→5 V buck。這顆 buck 和 camera 用的 3.3 V buck 就擺在 RJ45 和 camera 連接器之間，離 I2C 電壓轉換器只有一兩公分。
- 電壓轉換器兩側的參考電壓來自不同穩壓器：SoC 端是 SOM 的 1.8 V（PMIC），轉換器的 VCCA 是載板另一顆 LDO 的 1.8 V。負載變動時兩者可能各自抖動。
- 自動方向（auto-direction）電壓轉換器靠偵測邊緣決定方向，對毛刺和參考電壓抖動比較敏感。2.2 kΩ 上拉、100 kHz 的 I2C 也是高阻抗、慢邊緣的訊號。

## 4. Root Cause 分析

### 4.1 推論

綜合以上證據：

1. 寫入不是 Linux 送的，lpi2c 控制器也看不到異常（§3.3）。
2. 被改的位址都是 sensor 的目前位址指標，而且傳輸時間沒有變長（§3.2）。
3. 只在 stream-on 後 0.05–0.5 秒內發生，降速反而更容易發生（§3.3、§3.4）。
4. 需要多種負載疊加才會發生，換板子、換 cable、關 DMA 都會發生（§3.4、§3.5）。

最符合的解釋是：**sensor 開始出圖時電流變化，加上 burn-in 的整體負載和網路活動，讓 SCL 上出現毛刺或電源抖動。sensor 多看到了 SCL 邊緣，把匯流排上後續的訊號誤認為寫入資料，寫到目前的位址指標上。**

```text
burn-in 測項（含 ethernet）     →  SOM 負載、buck 切換電流變動
                                       │
camera stream-on（0.05–0.5 s）   →  sensor 電流突然增加
                                       │
                                       ▼
            I2C_CAM1 SCL 毛刺 / 電壓轉換器參考電壓抖動
                                       │
                                       ▼
   OV5640 多數到 SCL 邊緣 → 把後續訊號當成寫入 → 寫到目前位址指標
                                       │
                                       ▼
       0x3000–0x3008 等 reset/clock 暫存器被改 → MIPI 停在 LP-11
```

這個推論還沒有量測證實。「只有 camera + CPU/GPU/VPU/NPU」這組 SOM 最耗電的組合，177 分鐘也是 0 次，所以不單純是總電流大小，可能還和電流變化的頻率或時序有關。

### 4.2 另一種卡住

統計時另外看到少數幾次「camera 連續逾時，但監控的暫存器都正常」。讀出 1536 個暫存器和正常出圖比對，只有統計和狀態類的差異（白平衡統計全為 0），比較像 sensor 內部停止輸出。42 次事件中佔 2 次，發生條件和暫存器被改的那種相同，可能是改到了沒有監控的範圍。

## 5. 解決方案

### 5.1 測試手法：持續串流

改壞的永遠是「正在寫的那個暫存器」，而且只在 stream-on 時發生。burn-in 原本每 4–5 秒就重新啟動一次 pipeline，每次啟動都要重新寫一整份初始化表：

| 時機 | sensor I2C 寫入量 |
|---|---|
| 每次啟動（stream-on） | 約 319 次，0.6 秒內送完 |
| 串流中（libcamera 調整曝光、增益） | 每秒約 2～3 次 |
| 舊做法平均（每 4～5 秒重啟一次） | 每秒約 70 次 |

改成持續串流後，stream-on 只發生一次，寫入量約少 30 倍。串流中只寫曝光和增益，就算被寫壞也只是幾張畫面亮度跳動，下一次調整就會覆蓋，不會再改到 `0x3000` 這類控制 reset 的暫存器。

burn-in 的 camera 測項加一個參數 `CAMERA_CONTINUOUS=1`：

- 第一輪在背景啟動 pipeline，每秒存一張 JPEG，只保留最新 3 張，不顯示在螢幕上，避免和顯示測項衝突。
- 之後每一輪只檢查三件事：pipeline 還活著、最新一張在 10 秒內產生、和上一次檢查的畫面不完全相同（凍結偵測）。
- 任一項失敗就回報 FAIL，記下 gst/libcamera 的錯誤，關掉 pipeline，下一輪重新啟動。
- 單次測試或沒設參數時，維持原本每次啟動拍照的做法。

```bash
gst-launch-1.0 -q libcamerasrc ! video/x-raw,width=1280,height=720 ! \
	videorate drop-only=true max-rate=1 ! jpegenc ! \
	multifilesink location="$CAMERA_STREAM_DIR/frame-%06d.jpg" max-files=3 \
	</dev/null >"$CAMERA_LOGFILE" 2>&1 &
```

```bash
newest=$(ls -t "$CAMERA_STREAM_DIR"/frame-*.jpg 2>/dev/null | head -n 1)
age=$(($(date +%s) - $(stat -c %Y "$newest")))
sum=$(md5sum <"$newest" | cut -c1-32)
# age > CAMERA_STALE_SEC → FAIL（沒有新畫面）
# 檔名不同但 md5 相同 → FAIL（畫面凍結）
```

### 5.2 驗證結果

| 項目 | 條件 | 結果 |
|---|---|---|
| 改版前 | 100 kHz、全部測項、eth0 有連線 | 約 35 分鐘一次 |
| 改版後 | 同條件 3 小時，出事時不重啟 burn-in，完全交給 camera 測項自己處理 | 0 次失敗 |
| camera 檢查 | 同上 | 2472 次全部 PASS，相鄰兩次最多間隔 5 秒 |
| 串流 | 同上 | 從頭到尾只啟動一次，連續產生 10782 張 frame（1 fps） |
| 暫存器檢查 | 同上 | 26346 次，0 次被改壞 |
| dmesg | 同上 | 沒有 camera 或 I2C 錯誤 |
| 異常處理 | 中途強制殺掉 pipeline | 該輪 FAIL，下一輪自動恢復 PASS |

依改版前的頻率，3 小時應該出事約 5 次。

### 5.3 考慮過但沒採用的做法

| 做法 | 問題 |
|---|---|
| I2C 降速 | 反而更容易發生 |
| 關掉 lpi2c 的 DMA | 一樣會發生 |
| 換 cable | 一樣會發生 |
| 失敗時在 driver 裡重寫初始化表 | 只能事後補救，而且 driver 看不到異常，無從判斷何時要重寫 |
| 改 layout 或換 I2C 專用電壓轉換器 | 需要量測確認根因，量產前來不及 |

### 5.4 硬體量測建議

用 100 kHz + 全部測項重現（約 35 分鐘一次），4 通道同時看：

1. 電壓轉換器 camera 端的 SCL
2. 電壓轉換器 SoC 端的 SCL
3. 電壓轉換器附近的 3.3 V 或 VCCA（1.8 V）
4. buck 切換節點或電壓轉換器附近的地

用 glitch / runt trigger 等事件，或先看每次 stream-on 的波形，重點是 SCL 毛刺是否和 buck 切換或電源抖動同步。0402 的上拉電阻太小，無法直接夾探棒，需要焊線。

依量測結果，可以評估的硬體修改：SCL 加 RC 濾波或串聯電阻、改用 I2C 專用的電壓轉換 IC、加強 camera 電源濾波。

## 6. 結論與建議

1. **先確認是誰改了暫存器。** 「camera 卡住」很容易先懷疑 CSI/ISI driver。直接讀 sensor 暫存器並和正常狀態比對，才發現是 sensor 自己的狀態被改壞。手動寫入同樣的值能重現，就能確定方向。
2. **driver 看不到的錯誤，要在匯流排兩端都記錄。** 在 I2C core 記錄所有傳輸、在 lpi2c 記錄控制器狀態，才能確定「Linux 沒送、控制器也沒異常」，把問題推到匯流排或 sensor 端。
3. **發生頻率不穩定時，不要用單輪結果下結論。** 這個問題從 30 秒到 3.5 小時都會發生，好幾次單輪實驗的結論，事後都被推翻。用多種組合輪流跑、算平均發生頻率，再用機率判斷 0 次是否有意義，才找到 ethernet 這個條件。
4. **降速不一定比較穩。** 如果干擾只在某段時間內出現，降速讓每筆傳輸變長，反而更容易碰上。這也可以拿來當快速重現的方法。
5. **測試手法避開不等於解決。** 持續串流讓 burn-in 不再碰到這個問題，但客戶應用如果頻繁開關 camera（例如每次拍照都重新啟動），仍可能遇到。這個問題應列為已知問題，硬體耦合仍需量測確認。

## 附錄

### A. 常用指令

```bash
i2ctransfer -f -y 2 w2@0x3c 0x30 0x04 r1
i2ctransfer -f -y 2 w3@0x3c 0x30 0x04 0xff

cat /sys/bus/i2c/devices/2-003c/power/runtime_status
grep -i isi /proc/interrupts

cam -l
gst-launch-1.0 libcamerasrc num-buffers=30 ! fakesink
```

### B. 參考資料

- Linux：`drivers/media/i2c/ov5640.c`
- Linux：`drivers/i2c/busses/i2c-imx-lpi2c.c`
- Linux：`drivers/media/platform/nxp/imx8-isi/`
- OV5640 datasheet：system control 暫存器（`0x3000`–`0x3008`）、MIPI control（`0x4800`）
- NXP i.MX95 Reference Manual：LPI2C、MIPI CSI-2
