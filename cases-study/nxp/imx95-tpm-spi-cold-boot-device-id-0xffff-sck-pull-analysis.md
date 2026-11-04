# i.MX95 SPI TPM 冷開機 probe 讀到 device-id 0xFFFF 分析（SCK pad pull-up 造成第一次傳輸錯位）

## 1. 問題概述

一片 i.MX95 SMARC 自製板，接在 SPI 上的 TPM 在開機時 probe 印出：

```text
tpm_tis_spi spi0.0: 2.0 TPM (device-id 0xFFFF, rev-id 0)
tpm tpm0: A TPM error (256) occurred attempting the self test
tpm tpm0: starting up the TPM manually
```

正常應該是 `device-id 0x3`（ST33KTPM 系列）。

已知的現象：

- 開機之後 TPM 都可以正常使用，`tpm2_getrandom` 等指令沒有問題。
- 軟體重開機（`reboot`）讀到的是正常的 `0x3`。
- 按 Reset 鍵或拔插電源後的開機，讀到 `0xFFFF`。

一開始的判斷是「冷開機時 TPM 被 reset 腳壓住，Linux probe 時才剛放開，還沒準備好就被讀取」，於是在 SPL 補上開機時對 TPM_RST 送 pulse。這修好了另外兩個問題，但 `0xFFFF` 仍然存在。逐一排除時間、reset 腳、CS 腳之後，最後查出：**SPI 的 SCK pad 設成 pull-up，但 TPM 用 SPI mode 0（SCK 閒置為 low）。SoC 剛 reset 後的第一次 SPI 傳輸，TPM 會多看到一個 clock 邊緣，讀回的資料錯位。把 SCK 改成 pull-down 就修好了。**

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55，另有 Cortex-M33 執行 System Manager） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP LF_6.12.34_2.1.0（Yocto 5.2，fsl-imx-wayland） |
| Kernel | 6.12.34（linux-imx lts-next） |
| U-Boot | 2025.04（NXP lf_v2025.04 系列） |
| TPM | ST33KTPM2X32（TPM 2.0），1.8V I/O |
| SPI | LPSPI7，CS 用 GPIO（`cs-gpios = <&gpio2 4 GPIO_ACTIVE_LOW>`），10 MHz，mode 0 |
| TPM_RST | `XSPI1_SS0_B` pad，GPIO5_IO10 |
| TPM PIRQ | `XSPI1_SS1_B` pad，GPIO5_IO11，外部 10K pull-up |

原本 kernel DTS 的 SPI pin 設定：

```dts
pinctrl_lpspi7: lpspi7grp {
	fsl,pins = <
		IMX95_PAD_GPIO_IO04__GPIO2_IO_BIT4	0x3fe
		IMX95_PAD_GPIO_IO05__LPSPI7_SIN		0x3fe
		IMX95_PAD_GPIO_IO06__LPSPI7_SOUT	0x3fe
		IMX95_PAD_GPIO_IO07__LPSPI7_SCK		0x3fe
	>;
};
```

i.MX95 pad control 的相關欄位：

| bit | 欄位 | 說明 |
|---|---|---|
| 6:1 | DSE | 驅動強度 |
| 8:7 | FSEL1 | slew rate |
| 9 | PU | pull-up |
| 10 | PD | pull-down |

所以 `0x3fe` 是「最大驅動強度 + fast slew + **pull-up**」，`0x5fe` 是同樣設定但改成 **pull-down**。

## 3. 除錯過程

### 3.1 0xFFFF 的影響

`tpm_tis_core_init()` 一開始就讀 `TPM_DID_VID`，而且是在 `wait_startup()` 之前：

```c
	rc = tpm_tis_read32(priv, TPM_DID_VID(0), &vendor);
	if (rc < 0)
		return rc;

	priv->manufacturer_id = vendor;
	...
	if (wait_startup(chip, 0) != 0) {
```

dmesg 印出的 device-id 就是這次讀到的值。之後的存取都正常，所以 TPM 能用。但 `manufacturer_id` 錯了，driver 的廠商相關處理就不會套用，例如 `tpm_tis_req_canceled()` 裡 ST 晶片專用的取消判斷：

```c
		case TPM_VID_STM:
			return (status == (TPM_STS_VALID | TPM_STS_COMMAND_READY));
```

日常使用看不出差異，但代表開機時 SPI 上確實有一筆錯誤的傳輸，值得查清楚。

### 3.2 依開機方式分類

i.MX95 的 SPL 會印出 System Manager 回報的開機原因：

```text
SYS Boot reason: por, origin: -1, errid: -1
LM Boot reason: sw, origin: -1, errid: -1
```

`SYS` 是整顆 SoC，`LM` 是 Linux 所在的 logical machine。軟體重開機只 reset LM，按 Reset 鍵或上電則是整顆 SoC reset（`LM Boot reason: por`）。

| 開機方式 | LM Boot reason | device-id |
|---|---|---|
| 上電 | por | 0xFFFF |
| 按 Reset 鍵 | por | 0xFFFF |
| `reboot` | sw | 0x3 |

### 3.3 第一個假設：TPM 被 reset 腳壓住

查 RM，`XSPI1_SS0_B`（TPM_RST）的 reset 預設是 GPIO 功能、pad 值 `0x51E`（pull-down）。線路圖上 TPM_RST 的外部 pull-up 看起來沒有上件。推論是冷開機後 TPM_RST 被 pad 的 pull-down 拉在 low，TPM 一直處於 reset，直到 Linux 套用 pinctrl 才放開，接著 driver 馬上讀 ID。

檢查 SPL 時也發現：

- SPL 沿用 NXP EVK 的 XSPI NOR flash reset 程式，把 GPIO5_IO11 設成輸出 high。這塊板子上那支腳是 TPM 的 PIRQ，是 TPM 的輸出，所以 kernel 拿不到中斷：`TPM interrupt not working, polling instead`。
- 沒有任何程式在開機時 reset TPM，軟體重開機時 TPM 甚至不會被 reset，PCR 值會保留到下一次開機。

於是在 SPL 拿掉 EVK 的 NOR reset，改成開機時對 TPM_RST 送一個 10 ms 的 low pulse，然後維持 high。

結果：

| 項目 | 結果 |
|---|---|
| PIRQ 中斷 | 修好，所有開機方式 `/proc/interrupts` 的 tpm0 計數都會增加 |
| 軟體重開機 | 現在會 reset TPM（出現 `TPM error (256)`），device-id 0x3 |
| 按 Reset 鍵 | **仍然 0xFFFF** |

所以 reset 腳的推論只對了一部分，另有原因。

### 3.4 排除時間因素

在 U-Boot 擋下自動開機，停 60 秒再 `boot`。如果是 TPM 還沒準備好，等 60 秒應該就會正常。

結果仍然是 `0xFFFF`。不是時間問題。

### 3.5 一個誤導：PDIR 讀值

在 U-Boot 讀 GPIO5 的暫存器（PDOR +0x40、PDIR +0x50、PDDR +0x54）：

```text
43850040: 00000400              PDOR：bit10 輸出 high
43850050: 00027869 00000400     PDIR：bit10 = 0；PDDR：bit10 為輸出
```

TPM_RST 設成輸出 high，PDIR 讀回卻是 0，看起來像是腳被外部拉住。但軟體重開機（device-id 正常）時讀到的 PDIR bit10 也是 0。在 U-Boot 用 `gpio set` 把 CS 設成輸出 high 後，CS 的 PDIR bit 也從 1 變成 0。

**這個 GPIO 在腳位設成輸出時，PDIR 讀回固定是 0，不能拿來判斷腳位實際電位。** 用它推論會得到錯的結論。

### 3.6 排除 CS 和 reset 順序

SoC reset 後 CS（GPIO2_IO04）是輸入，軟體重開機時則維持前一次 Linux 留下的輸出 high。懷疑 TPM 在 CS 被選中時離開 reset，導致 SPI 狀態錯亂。

在 U-Boot 先 `gpio set GPIO2_4`（CS high），再重送一次 TPM_RST pulse，然後開機。

結果仍然是 `0xFFFF`。跟 CS 狀態和 reset 順序無關。

### 3.7 確認只有第一次傳輸出錯

在讀到 `0xFFFF` 的同一次開機裡：

```bash
tpm2_getrandom --hex 8
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/unbind
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/bind
dmesg | tail -1
```

```text
tpm_tis_spi spi0.0: 2.0 TPM (device-id 0x3, rev-id 0)
```

TPM 本身正常，重新 probe 也讀到正確的 ID。只有 SoC reset 後 Linux 的第一筆 SPI 傳輸出錯。

### 3.8 整理

| 開機方式 | SoC（pad、LPSPI） | TPM 在 SPL 前的狀態 | device-id |
|---|---|---|---|
| 上電 | 全新 | 剛上電 | 0xFFFF |
| 按 Reset 鍵 | 全新 | 原本在運作，被 SPL reset | 0xFFFF |
| `reboot` | 維持前一次 Linux 的設定 | 原本在運作，被 SPL reset | 0x3 |
| 同一次開機重新綁定 driver | 已初始化 | 已在運作 | 0x3 |

決定結果的是「SoC 這一側是不是全新的」，跟 TPM 的狀態無關。範圍縮小到 SPI 控制器或 pad 在第一次傳輸時的行為。

註：燒錄 image 後的第一次開機曾讀到一次 `0x3`。那次開機前經過燒錄流程，SoC 的狀態和一般上電不同，沒有再深究。之後一般上電一律是 `0xFFFF`。

### 3.9 SCK pad 的 pull 方向

TPM 用 SPI mode 0：CPOL = 0，SCK 閒置時要是 low。但 SCK pad 設成 `0x3fe`，是 pull-up。

推測的時序：

```text
                  LPSPI 尚未驅動 SCK           LPSPI 開始驅動
SCK   ─────────── high（pad pull-up）───────┐______________╱‾╲_╱‾╲_ ...
                                            ↑
                                    多出來的下降緣
CS    ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲_______________________________
                                    （GPIO CS 先拉 low）
```

- SoC reset 後，LPSPI7 還沒啟用，SCK 只靠 pad 的 pull-up 維持 high。
- spi core 先把 GPIO CS 拉 low，LPSPI 才開始傳輸，把 SCK 驅動成 mode 0 的 low。
- TPM 在 CS 有效時看到一個多的 SCK 邊緣，之後的位元全部錯位，讀回 0xFF。
- LPSPI 開始驅動後，SCK 平時就維持在 low，之後的傳輸都正常。
- 軟體重開機時 LM reset 沒有 reset 這組 pad 和 LPSPI，SCK 已經是 low，所以不會出現這個邊緣。

這個時序是從實驗結果推出來的，沒有用示波器量過 SCK 波形。

### 3.10 驗證

直接修改板子上的 DTB，只把 SCK 的 pad 值從 `0x3fe` 改成 `0x5fe`：

```bash
fdtput -t x imx95-board.dtb /firmware/scmi/protocol@19/lpspi7grp fsl,pins \
	0x20 0x224 0 0 0 0x3fe \
	0x24 0x228 0 4 0 0x3fe \
	0x28 0x22c 0 4 0 0x3fe \
	0x2c 0x230 0 4 0 0x5fe
```

每 6 個 cell 是一支腳：mux 暫存器、config 暫存器、input 暫存器、mux 值、input 值、pad 值。最後一組就是 `GPIO_IO07__LPSPI7_SCK`。

| DTB | SoC reset 開機（上電或 Reset 鍵） |
|---|---|
| 原本：SCK `0x3fe`（pull-up） | 0xFFFF，4/4 |
| 修改：SCK `0x5fe`（pull-down） | **0x3，2/2** |

`TPM error (256)` 仍然會出現，這是正常的：SPL 剛 reset 過 TPM，還沒執行 `TPM2_Startup`，kernel 會自己補做。

## 4. Root Cause 分析

### 4.1 原因

SPI mode 0 的 SCK 閒置電位是 low。pad 的 pull 方向應該跟閒置電位一致，這樣控制器還沒驅動之前，線上的電位就已經正確。

原本 DTS 把 SPI 四支腳都設成同一個值 `0x3fe`（pull-up），對 CS（低有效，閒置 high）和 MISO 是合理的，但對 mode 0 的 SCK 是反的。SoC reset 後控制器第一次接手 SCK 時，就會產生一個多餘的 clock 邊緣。

### 4.2 為什麼一開始的假設不對

| 假設 | 驗證方法 | 結果 |
|---|---|---|
| TPM 被 reset 腳壓住，Linux 才放開 | SPL 加 TPM_RST pulse | 修好軟體重開機的 reset，POR 仍失敗 |
| TPM 還沒準備好 | U-Boot 停 60 秒 | 仍失敗 |
| CS 在 reset 期間被選中 | U-Boot 先拉高 CS 再 reset TPM | 仍失敗 |
| TPM 本身異常 | 同一次開機重新綁定 driver | 正常 |
| SCK pull 方向錯 | 改成 pull-down | 修好 |

第一個假設也不是完全錯的。SPL 加上 TPM_RST pulse 之後，每次開機 TPM 都會從乾淨的狀態開始，軟體重開機時 PCR 不會再殘留上一次的值，這部分仍然保留。

### 4.3 為什麼很容易被忽略

- TPM 開機後都能用，只有 probe 的一行 log 不對。
- 軟體重開機正常，在開發時最常用的重開方式下看不到問題。
- 用同一組值設定整組 SPI pin 很常見，看起來不像錯誤。

## 5. 解決方案

### 5.1 Kernel DTS

```diff
--- a/arch/arm64/boot/dts/freescale/imx95-board.dts
+++ b/arch/arm64/boot/dts/freescale/imx95-board.dts
@@ -406,7 +406,7 @@
 			IMX95_PAD_GPIO_IO04__GPIO2_IO_BIT4	0x3fe
 			IMX95_PAD_GPIO_IO05__LPSPI7_SIN		0x3fe
 			IMX95_PAD_GPIO_IO06__LPSPI7_SOUT	0x3fe
-			IMX95_PAD_GPIO_IO07__LPSPI7_SCK		0x3fe
+			IMX95_PAD_GPIO_IO07__LPSPI7_SCK		0x5fe
 		>;
 	};
```

只改 SCK。CS 是低有效，維持 pull-up 讓它在控制器接手前保持「不選中」。MOSI 和 MISO 不影響 clock 邊緣，這次沒有改。

### 5.2 SPL

和這個問題一起處理的 SPL 修改：

- 拿掉 EVK 的 XSPI NOR flash reset（這塊板子沒有 NOR flash，那支腳是 TPM PIRQ），並在 U-Boot DTS 關掉 `&flexspi1`。原本它的 pinctrl 會把 `XSPI1_SS0_B`（TPM_RST）切成 FlexSPI 的 CS。
- 開機時對 TPM_RST 送 10 ms 的 low pulse，讓 TPM 每次開機都從 reset 狀態開始。

```c
static void tpm_reset(void)
{
	int ret;
	struct gpio_desc desc;

	ret = dm_gpio_lookup_name("GPIO5_10", &desc);
	if (ret)
		return;

	ret = dm_gpio_request(&desc, "TPM_RST");
	if (ret)
		return;

	dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT | GPIOD_IS_OUT_ACTIVE | GPIOD_ACTIVE_LOW);
	mdelay(10);
	dm_gpio_set_value(&desc, 0);
}
```

### 5.3 Yocto 整合

kernel 和 U-Boot 的修改都用 patch 加進 BSP layer 的 bbappend：

```text
SRC_URI:append = " file://0002-arm64-dts-imx95-board-pull-the-TPM-SPI-clock-down.patch"
```

patch 檔要有 `Upstream-Status:` 標記，不然 Yocto 5.x 的 `patch-status` QA 會報錯中止。

## 6. 結論與建議

1. **SPI pad 的 pull 方向要配合 SPI mode。** mode 0 / 1 的 SCK 用 pull-down，mode 2 / 3 用 pull-up。GPIO 當 CS 時用 pull-up。不要整組 pin 套同一個值。
2. **依開機方式分類。** 上電、Reset 鍵、軟體重開機在 i.MX95 上 reset 的範圍不同，SPL 的 `Boot reason` 可以直接看出是哪一種。只在 SoC reset 時發生的問題，要往 pad 和周邊控制器的初始狀態找。
3. **重新綁定 driver 是很好的對照。** 同一次開機裡重新 probe，可以分辨是裝置本身的問題，還是只有第一次存取出錯。
4. **不要用 GPIO PDIR 判斷輸出腳的電位。** 這次的 GPIO 在腳位設成輸出時，PDIR 固定讀回 0。
5. **一次只改一個變數。** 直接用 `fdtput` 修改板子上的 DTB，不用重編 kernel 就能驗證 pad 設定。
6. **開機時 reset TPM。** 沒有外部 pull-up 或 reset 電路時，SPL 要主動 reset TPM，否則軟體重開機時 TPM 狀態（包括 PCR）會殘留到下一次開機。

## 附錄

### A. 常用指令

U-Boot：

```text
md.l 0x43850040 1      GPIO5 PDOR
md.l 0x43850050 2      GPIO5 PDIR、PDDR
md.l 0x43810040 1      GPIO2 PDOR
md.l 0x43810050 2      GPIO2 PDIR、PDDR
gpio set GPIO2_4
gpio clear GPIO5_10; sleep 0.1; gpio set GPIO5_10
```

Linux：

```bash
dmesg | grep -E 'device-id|TPM error|Boot reason'
grep tpm /proc/interrupts
cat /sys/kernel/debug/gpio
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/unbind
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/bind
```

修改 DTB：

```bash
dtc -I dtb -O dts board.dtb | grep -n -A3 'lpspi7grp {'
fdtput -t x board.dtb <node> fsl,pins <cells...>
fdtget -t x board.dtb <node> fsl,pins
```

### B. 注意事項

- 從 A55 的 U-Boot 讀 IOMUXC（0x443C0000）會觸發 Synchronous Abort 並讓板子重開。i.MX95 的 IOMUXC 由 System Manager 管理，A55 要透過 SCMI pinctrl 設定。
- 修改板子上的 DTB 之前，先在開機分區留一份原檔備份。

### C. 參考資料

- i.MX 95 Applications Processor Reference Manual：External Signals and Pin Multiplexing、IOMUXC 暫存器
- Linux：`drivers/char/tpm/tpm_tis_core.c`（`tpm_tis_core_init`、`tpm_tis_req_canceled`）
- Linux：`drivers/spi/spi-fsl-lpspi.c`
- TCG PC Client Platform TPM Profile（PTP）Specification：SPI 介面
