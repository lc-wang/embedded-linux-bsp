# i.MX95 SPI TPM 中斷無法使用分析（SPL 沿用 EVK NOR flash reset 把 PIRQ 設成輸出）

## 1. 問題概述

一片 i.MX95 SMARC 自製板，接在 SPI 上的 TPM 每次開機 probe 都印出：

```text
tpm tpm0: Unable to request irq: 237 for probe
tpm tpm0: [Firmware Bug]: TPM interrupt not working, polling instead
```

TPM 可以用，但 driver 改用 polling，不靠 TPM 的 PIRQ 中斷。

已知的現象：

- 每種開機方式都一樣：上電、按 Reset 鍵、軟體重開機。
- `/proc/interrupts` 沒有 tpm0 這一行。
- kernel DTS 的設定看起來正確：TPM 節點的 `interrupts` 指到 GPIO5_IO11，pinctrl 也把那支腳設成 GPIO、pull-up。

一開始懷疑是 kernel DTS 的中斷觸發方式或 pinctrl 寫錯。實際分析後，kernel 的設定沒有問題：**U-Boot SPL 從 NXP EVK 沿用了一段 XSPI NOR flash 的 reset 程式，把 GPIO5_IO11 設成輸出 high。這塊板子沒有 NOR flash，那支腳接的是 TPM 的 PIRQ。Linux 開機時那支 GPIO 還是輸出狀態，gpiolib 不允許把輸出腳當中斷，所以 request irq 失敗。**

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP LF_6.12.34_2.1.0（Yocto 5.2，fsl-imx-wayland） |
| Kernel | 6.12.34（linux-imx lts-next） |
| U-Boot | 2025.04（NXP lf_v2025.04 系列），板子的 SPL 從 i.MX95 EVK 複製修改 |
| TPM | ST33KTPM2X32（TPM 2.0），接在 LPSPI7 |
| TPM PIRQ | `XSPI1_SS1_B` pad → GPIO5_IO11，TPM 的開汲極輸出，外部 10K pull-up |
| TPM_RST | `XSPI1_SS0_B` pad → GPIO5_IO10 |
| GPIO driver | `gpio-vf610` |

kernel DTS 的 TPM 節點：

```dts
tpm@0 {
	compatible = "st,st33htpm-spi", "tcg,tpm_tis-spi";
	pinctrl-names = "default";
	pinctrl-0 = <&pinctrl_tpm_gpio>;
	interrupt-parent = <&gpio5>;
	interrupts = <11 IRQ_TYPE_LEVEL_LOW>;
	reg = <0>;
	spi-max-frequency = <10000000>;
};

pinctrl_tpm_gpio: tpmgpiogrp {
	fsl,pins = <
		IMX95_PAD_XSPI1_SS0_B__GPIO5_IO_BIT10	0x31e
		IMX95_PAD_XSPI1_SS1_B__GPIO5_IO_BIT11	0x3fe
	>;
};
```

## 3. 除錯過程

### 3.1 為什麼 request irq 會失敗

`tpm_tis_probe_irq_single()` 用 `devm_request_threaded_irq()` 申請中斷，失敗就印出 `Unable to request irq`，最後改用 polling。

GPIO 當中斷來源時，request irq 會經過 gpiolib 的 `gpiochip_reqres_irq()` → `gpiochip_lock_as_irq()`（`drivers/gpio/gpiolib.c`）：

```c
	if (!gc->can_sleep && gc->get_direction) {
		int dir = gpiod_get_direction(desc);
		...
	}

	/* To be valid for IRQ the line needs to be input or open drain */
	if (test_bit(FLAG_IS_OUT, &desc->flags) &&
	    !test_bit(FLAG_OPEN_DRAIN, &desc->flags)) {
		chip_err(gc,
			 "%s: tried to flag a GPIO set as output for IRQ\n",
			 __func__);
		return -EIO;
	}
```

`gpio-vf610` 有提供 `get_direction`，會直接讀硬體的方向暫存器（PDDR）。只要這支腳在硬體上還是輸出，就會被拒絕。

kernel 這邊沒有任何 driver 會把 GPIO5_IO11 設成輸出：pinctrl 只設 pad 的 mux 和 pull，不改 GPIO 方向。所以輸出狀態只可能是開機前留下來的。

### 3.2 查 U-Boot

板子的 SPL（`board/<vendor>/<board>/spl.c`）在 `board_init_f()` 最後呼叫：

```c
static void flexspi_nor_reset(void)
{
	int ret;
	struct gpio_desc desc;

	/* 15x15 EVK use M.2 QSPI card, not support booting */
	if (IS_ENABLED(CONFIG_TARGET_IMX95_15X15_EVK))
		return;

	ret = dm_gpio_lookup_name("GPIO5_11", &desc);
	...
	ret = dm_gpio_request(&desc, "XSPI_RST_B");
	...
	/* assert the XSPI_RST_B */
	dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT | GPIOD_IS_OUT_ACTIVE | GPIOD_ACTIVE_LOW);
	udelay(200); /* 50 ns at least, so use 200ns */
	dm_gpio_set_value(&desc, 0); /* deassert the XSPI_RST_B */
}
```

這段和 NXP i.MX95 EVK 的 `board/freescale/imx95_evk/spl.c` 完全一樣。EVK 上 `XSPI1_SS1_B`（GPIO5_IO11）接的是 XSPI NOR flash 的 reset，所以 SPL 會先送一個 reset pulse，最後維持輸出 high（不 reset）。

這塊板子的 SOM 沒有 NOR flash，`XSPI1_SS1_B` 接到 TPM 的 PIRQ。結果：

- SPL 把 GPIO5_IO11 設成輸出並驅動 high，一直維持到 Linux。
- PIRQ 是 TPM 的輸出，SoC 和 TPM 兩邊都在驅動同一條線。TPM 拉 low 時就會互相衝突。
- Linux 申請中斷時，這支腳還是輸出，被 gpiolib 拒絕。

U-Boot 的 DTS 也有同樣的 EVK 殘留：

- `&flexspi1` 是 `okay`，下面有 NOR flash 節點，`reset-gpios = <&gpio5 11 GPIO_ACTIVE_LOW>`。
- `pinctrl_flexspi1` 把 `XSPI1_SS0_B` 設成 FlexSPI 的 CS。在這塊板子上那支腳是 TPM_RST。
- `pinctrl_hog` 也包含 `IMX95_PAD_XSPI1_SS1_B__GPIO5_IO_BIT11`。

### 3.3 在 Linux 上驗證

不改 U-Boot，直接在 Linux 把 GPIO5_IO11 改回輸入，再讓 TPM driver 重新 probe：

```bash
gpioget <gpio5 chip> 11
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/unbind
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/bind
```

`gpioget` 會以輸入方式申請這支腳，硬體方向就變成輸入。重新 probe 之後：

- 沒有出現 `Unable to request irq`。
- `/proc/interrupts` 出現 tpm0，送 TPM 指令時計數會增加。

確認原因就是這支 GPIO 的方向。

## 4. Root Cause 分析

### 4.1 原因

```text
SPL board_init_f()
└─ flexspi_nor_reset()                    ← 從 EVK 沿用
   └─ GPIO5_IO11 設成輸出，驅動 high（EVK：NOR reset 放開）
                                              （本板：TPM PIRQ）
U-Boot → Linux
   GPIO5 的方向暫存器沒有被 reset，GPIO5_IO11 仍是輸出

Linux tpm_tis_spi probe
└─ devm_request_threaded_irq()
   └─ gpiochip_lock_as_irq()
      └─ 方向是輸出 → -EIO
         → "Unable to request irq: 237 for probe"
         → "TPM interrupt not working, polling instead"
```

這是 BSP 移植時的殘留。i.MX95 EVK 和自製板用了同一組 pad，但接的東西不同：EVK 是 NOR flash 的 reset（SoC 輸出），自製板是 TPM 的中斷（TPM 輸出）。

### 4.2 為什麼不容易發現

- TPM 照樣能用，只是改成 polling，功能測試看不出差異。
- kernel DTS 寫得正確，只看 kernel 端找不到問題。
- `Unable to request irq` 是 `dev_info` 等級，`Firmware Bug` 那行又容易被當成 TPM 韌體的問題。
- 這段 SPL 程式本身沒有錯，在 EVK 上是正確的。

### 4.3 影響

- TPM 改用 polling，每個指令都要定時讀狀態暫存器，延遲和 CPU 使用率都比較高。
- PIRQ 線上 SoC 一直驅動 high，TPM 拉 low 時兩邊衝突。TPM 的 PIRQ 是開汲極輸出，外部有 10K pull-up，SoC 不應該驅動這條線。
- `&flexspi1` 一旦被 probe，`pinctrl_flexspi1` 就會把 TPM_RST 那支腳切成 FlexSPI 的 CS，TPM 的 reset 狀態改由 FlexSPI 控制器決定，而不是 GPIO。

## 5. 解決方案

### 5.1 SPL

拿掉 `flexspi_nor_reset()` 和它的呼叫：

```diff
--- a/board/<vendor>/<board>/spl.c
+++ b/board/<vendor>/<board>/spl.c
@@ -47,33 +47,6 @@ void spl_board_init(void)
 		printf("Fail to start RNG: %d\n", ret);
 }
 
-static void flexspi_nor_reset(void)
-{
-	...
-	ret = dm_gpio_lookup_name("GPIO5_11", &desc);
-	...
-	dm_gpio_set_dir_flags(&desc, GPIOD_IS_OUT | GPIOD_IS_OUT_ACTIVE | GPIOD_ACTIVE_LOW);
-	udelay(200); /* 50 ns at least, so use 200ns */
-	dm_gpio_set_value(&desc, 0); /* deassert the XSPI_RST_B */
-}
-
 void board_init_f(ulong dummy)
 {
@@ -112,8 +85,6 @@ void board_init_f(ulong dummy)
 	/* Will set ARM freq to max rate */
 	clock_init_late();
 
-	flexspi_nor_reset();
-
 	board_init_r(NULL, 0);
 }
```

### 5.2 U-Boot DTS

```diff
 &flexspi1 {
 	pinctrl-names = "default";
 	pinctrl-0 = <&pinctrl_flexspi1>;
-	status = "okay";
+	status = "disabled";
```

```diff
 	pinctrl_hog: hoggrp {
 		bootph-pre-ram;
 
 		fsl,pins = <
-			IMX95_PAD_XSPI1_SS1_B__GPIO5_IO_BIT11			0x3fe
 			...
```

`XSPI1_SS1_B` 的 reset 預設就是 GPIO 輸入，不需要 U-Boot 再設定，交給 kernel 的 pinctrl。

### 5.3 一起加入的 TPM reset

拿掉 NOR reset 後，SPL 改成在同一個位置對 TPM_RST（GPIO5_IO10）送 10 ms 的 low pulse，讓 TPM 每次開機都從 reset 狀態開始。這和 PIRQ 是兩件事，分成兩個 commit：

1. 拿掉 EVK 的 XSPI NOR reset，關掉 `&flexspi1`，pinctrl_hog 移除 `XSPI1_SS1_B`。
2. 開機時 reset TPM。

### 5.4 Yocto 整合

把兩個 commit 匯出成 patch，加進 BSP layer 的 U-Boot bbappend：

```text
SRC_URI:append = " file://0001-spl-drop-the-EVK-XSPI-NOR-reset.patch"
SRC_URI:append = " file://0002-spl-reset-the-TPM-at-boot.patch"
```

- 第二個 patch 依賴第一個，順序不能換。
- patch 檔要有 `Upstream-Status:` 標記，不然 Yocto 5.x 的 `patch-status` QA 會報錯中止。
- 改了 `SRC_URI` 之後 `do_patch` 會自己重跑，不需要先 clean。

### 5.5 驗證

燒入新 image 後，上電、按 Reset 鍵、軟體重開機都測過：

| 項目 | 修改前 | 修改後 |
|---|---|---|
| `Unable to request irq` | 每次開機都有 | 沒有 |
| `/proc/interrupts` 的 tpm0 | 沒有 | 有，送指令時計數增加 |
| `/sys/kernel/debug/gpio` 的 GPIO5_IO11 | 輸出 | `in hi IRQ` |

例如執行一次 `tpm2_getrandom`，tpm0 的中斷計數從 7 增加到 9。

## 6. 結論與建議

1. **從 EVK 移植 board code 時，逐一檢查每個 GPIO 的用途。** pad 名稱相同不代表接的東西相同。特別是 SPL 和 U-Boot 這種早期程式，設定的 GPIO 方向會一直留到 Linux。
2. **kernel 拿不到 GPIO 中斷時，先看硬體方向。** `cat /sys/kernel/debug/gpio` 看那支腳是 `in` 還是 `out`，或在 U-Boot 讀 GPIO 的方向暫存器。gpiolib 會拒絕把輸出腳當中斷。
3. **用 `gpioget` 加重新綁定 driver 驗證。** 不用改 bootloader，就能確認是不是 GPIO 方向的問題。
4. **`Firmware Bug` 不一定是韌體的問題。** TPM 中斷拿不到就會印這行，原因可能在板子設定。
5. **輸出對輸出要避免。** PIRQ 是 TPM 的開汲極輸出，SoC 不應該驅動這條線，否則 TPM 拉 low 時會有電流衝突。

## 附錄

### A. 常用指令

Linux：

```bash
dmesg | grep -E 'tpm|irq'
grep tpm /proc/interrupts
cat /sys/kernel/debug/gpio
gpioinfo
gpioget <chip> <line>
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/unbind
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/bind
```

U-Boot（GPIO5 base 0x43850000）：

```text
md.l 0x43850054 1      PDDR：bit11 = 1 代表輸出
gpio status -a
```

### B. 參考資料

- U-Boot：`board/freescale/imx95_evk/spl.c`（`flexspi_nor_reset`）
- Linux：`drivers/gpio/gpiolib.c`（`gpiochip_lock_as_irq`、`gpiochip_reqres_irq`）
- Linux：`drivers/gpio/gpio-vf610.c`
- Linux：`drivers/char/tpm/tpm_tis_core.c`（`tpm_tis_probe_irq_single`）
- i.MX 95 Applications Processor Reference Manual：External Signals and Pin Multiplexing
