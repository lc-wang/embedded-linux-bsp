# i.MX95 SPI TPM 在 suspend/resume 後無法使用分析（tpm_tis_spi 缺少 resume callback）

## 1. 問題概述

一片 i.MX95 SMARC 自製板在測試時，短按電源鍵會讓系統進入 deep suspend。喚醒之後，所有 TPM 指令都失敗，例如：

```text
$ tpm2_getrandom --hex 8
ERROR:tcti:src/tss2-tcti/tcti-device.c:489:Tss2_Tcti_Device_Init() Failed to read response header fd 4, got errno 5: Input/output error
ERROR:tcti:src/tss2-tcti/tctildr.c:477:tctildr_init_context_data() Failed to instantiate TCTI
```

已知的現象：

- 每次 suspend/resume 之後都會發生（2/2），不論 suspend 前 TPM 有沒有在傳資料。
- `dmesg` 沒有任何 TPM 或 SPI 錯誤，suspend 和 resume 都顯示成功。
- 按 SoC Reset 重開機後 TPM 恢復正常（2/2）。
- 不重開機，把 `tpm_tis_spi` 從 SPI 裝置解除綁定再綁回去，TPM 也會恢復。

一開始的判斷是「TPM 在 deep suspend 時斷電或狀態遺失，resume 後需要重新 reset」，懷疑要在 resume 時對 TPM_RST 送 pulse。實際分析後，TPM 硬體在 suspend 期間沒有斷電，狀態也還在：**是 kernel 根本沒有送任何指令給 TPM。`tpm_tis_spi` 在沒有開 `CONFIG_TCG_TIS_SPI_CR50` 時沒有 resume callback，chip 一直停在 suspended 狀態，所有存取都被 TPM core 直接擋下並回傳 `-EIO`。**

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP LF_6.12.34_2.1.0（Yocto 5.2，fsl-imx-wayland） |
| Kernel | 6.12.34（linux-imx lts-next） |
| TPM | ST33KTPM2X32（TPM 2.0），接在 LPSPI7，`spi-max-frequency = <10000000>` |
| Driver | `tpm_tis_spi`（`compatible = "st,st33htpm-spi", "tcg,tpm_tis-spi"`） |
| Kernel config | `CONFIG_TCG_TIS_SPI=y`，`CONFIG_TCG_TIS_SPI_CR50` 未設定，`CONFIG_PM_SLEEP=y` |
| User space | tpm2-tss 4.1.3、tpm2-tools |
| Suspend | `mem_sleep` = deep，電源鍵短按由 systemd-logind 觸發（`HandlePowerKey=suspend`） |

## 3. 除錯過程

### 3.1 先確認是 TPM 沒回應，還是根本沒送出去

`-EIO` 有可能是 SPI 傳輸失敗、TPM 回應逾時，也可能是 kernel 自己回的。SPI core 在 sysfs 有每個裝置的傳輸統計，可以用來分辨：

```bash
s=/sys/bus/spi/devices/spi0.0/statistics
t1=$(cat $s/transfers)
tpm2_getrandom --hex 8
sleep 3
t2=$(cat $s/transfers)
echo "transfers in 3s: $((t2 - t1))"
```

| 狀態 | `tpm2_getrandom` | 3 秒內的 SPI 傳輸數 |
|---|---|---|
| 開機後正常 | 成功 | 數十次 |
| resume 之後 | `errno 5` | **0** |

`errors` 和 `timedout` 也都沒有增加。所以 resume 之後指令完全沒有送到 SPI 上，問題不在 SPI 控制器、也不在 TPM，而是 kernel 在更上層就擋掉了。

### 3.2 TPM 的狀態有沒有保留

在 resume 之後的同一次開機裡重新綁定 driver：

```bash
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/unbind
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/bind
```

重新 probe 之後 TPM 馬上可以用，而且 `dmesg` **沒有** 出現：

```text
tpm tpm0: A TPM error (256) occurred attempting the self test
tpm tpm0: starting up the TPM manually
```

`TPM_RC_INITIALIZE`（256）代表 TPM 剛被 reset、還沒執行 `TPM2_Startup`。重新綁定時沒有這個錯誤，表示 TPM 在 suspend 期間一直有電，也一直維持已 Startup 的狀態。所以「resume 後要對 TPM_RST 送 pulse」的方向是錯的，需要修的是 driver。

### 3.3 對照原始碼

TPM core 在 suspend 時會設一個旗標（`drivers/char/tpm/tpm-interface.c`）：

```c
int tpm_pm_suspend(struct device *dev)
{
	...
	if (chip->flags & TPM_CHIP_FLAG_TPM2) {
		tpm2_end_auth_session(chip);
		tpm2_shutdown(chip, TPM2_SU_STATE);
		goto suspended;
	}
	...
suspended:
	chip->flags |= TPM_CHIP_FLAG_SUSPENDED;
	tpm_put_ops(chip);
	...
}
```

這個旗標只有 `tpm_pm_resume()` 會清掉：

```c
int tpm_pm_resume(struct device *dev)
{
	...
	chip->flags &= ~TPM_CHIP_FLAG_SUSPENDED;
	...
}
```

而每個 TPM 指令都要先經過 `tpm_try_get_ops()`（`drivers/char/tpm/tpm-chip.c`），旗標還在就直接回傳 `-EIO`，不碰硬體：

```c
int tpm_try_get_ops(struct tpm_chip *chip)
{
	int rc = -EIO;
	...
	/* tmp_chip_start may issue IO that is denied while suspended */
	if (chip->flags & TPM_CHIP_FLAG_SUSPENDED)
		goto out_lock;
	...
}
```

`tpm_tis_spi` 的 PM ops（`drivers/char/tpm/tpm_tis_spi_main.c`）：

```c
static SIMPLE_DEV_PM_OPS(tpm_tis_pm, tpm_pm_suspend, tpm_tis_spi_resume);
```

`tpm_tis_spi_resume` 定義在 `drivers/char/tpm/tpm_tis_spi.h`：

```c
#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_TCG_TIS_SPI_CR50)
extern int tpm_tis_spi_resume(struct device *dev);
#else
#define tpm_tis_spi_resume	NULL
#endif
```

真正的 `tpm_tis_spi_resume()` 寫在 `tpm_tis_spi_cr50.c` 裡，只有開了 `CONFIG_TCG_TIS_SPI_CR50`（Google H1 / Cr50 安全晶片的支援）才會編進去。沒開的話 resume callback 就是 `NULL`。

## 4. Root Cause 分析

### 4.1 流程

```text
suspend
└─ tpm_pm_suspend()
   ├─ tpm2_shutdown(TPM2_SU_STATE)
   └─ chip->flags |= TPM_CHIP_FLAG_SUSPENDED

resume
└─ resume callback = NULL          ← 沒有人呼叫 tpm_pm_resume()
                                     TPM_CHIP_FLAG_SUSPENDED 一直留著

之後每個 TPM 指令
└─ tpm_try_get_ops()
   └─ flags & TPM_CHIP_FLAG_SUSPENDED → 回傳 -EIO，不做任何 SPI 傳輸
```

`tpm_pm_suspend()` 和 `tpm_pm_resume()` 必須成對出現，但 SPI 版本的 TIS driver 把 resume 這一半綁在 Cr50 的 Kconfig 上。一般的 SPI TPM（ST、Infineon、Nuvoton 等）不會開 Cr50 選項，所以 suspend 一次之後 TPM 就無法使用，直到 driver 重新 probe 或系統重開機。

### 4.2 為什麼 dmesg 沒有錯誤

- suspend 時 `tpm_pm_suspend()` 正常完成。
- resume 時 callback 是 `NULL`，PM core 直接跳過，不算錯誤。
- 之後的 `-EIO` 是在 `tpm_try_get_ops()` 回傳的，這條路不印 log。

所以只能從 user space 看到 `errno 5`，或從 SPI 統計看到沒有傳輸。

### 4.3 為什麼重新綁定或重開機會好

`TPM_CHIP_FLAG_SUSPENDED` 存在 `struct tpm_chip` 裡。重新綁定時舊的 chip 被釋放、重新配置一個新的，旗標自然清掉。TPM 本身一直正常，所以馬上就能用。

### 4.4 影響範圍

- 只影響 SPI 介面、走 `tpm_tis_spi` 的 TPM。I2C 的 `tpm_tis_i2c` 和 MMIO 的 `tpm_tis` 都有自己的 resume callback。
- 只在 `CONFIG_PM_SLEEP=y` 且 `CONFIG_TCG_TIS_SPI_CR50` 沒開時發生。
- NXP 6.18.37 系列的 kernel 程式一樣，也有這個問題。

## 5. 解決方案

### 5.1 上游修正

這個問題在 mainline 已經修好：

- commit `b4d1adc70471`："tpm: tpm_tis_spi: fix nodef CR50 tpm_tis_spi_resume is null"
- 第一個包含的版本是 v7.3

修法是沒開 Cr50 時，把 `tpm_tis_spi_resume` 直接指到 TIS core 的 `tpm_tis_resume()`：

```diff
--- a/drivers/char/tpm/tpm_tis_spi.h
+++ b/drivers/char/tpm/tpm_tis_spi.h
@@ -40,8 +40,12 @@ static inline int cr50_spi_probe(struct spi_device *spi)
 }
 #endif
 
-#if defined(CONFIG_PM_SLEEP) && defined(CONFIG_TCG_TIS_SPI_CR50)
-extern int tpm_tis_spi_resume(struct device *dev);
+#if defined(CONFIG_PM_SLEEP)
+	#if defined(CONFIG_TCG_TIS_SPI_CR50)
+		extern int tpm_tis_spi_resume(struct device *dev);
+	#else
+		#define  tpm_tis_spi_resume tpm_tis_resume
+	#endif
 #else
 #define tpm_tis_spi_resume	NULL
 #endif
```

`tpm_tis_resume()`（`drivers/char/tpm/tpm_tis_core.c`）會重新啟用 TPM 中斷，TPM 1.2 還會跑 self test，最後呼叫 `tpm_pm_resume()` 清掉 `TPM_CHIP_FLAG_SUSPENDED`。Cr50 版本的 `tpm_tis_spi_resume()` 本來也只是多重設一個喚醒計時，再呼叫同一個 `tpm_tis_resume()`。

這個 commit 沒有 `Fixes:` 和 `Cc: stable` 標記，所以沒有被 backport：

| 分支 | 狀態（2026-11 查詢） |
|---|---|
| mainline | v7.3 起已修 |
| stable 6.12.y（查到 6.12.115） | 未修 |
| stable 6.18.y（查到 6.18.60） | 未修 |
| NXP lf-6.12.34 / lf-6.18.37 | 未修 |

所以 6.12 和 6.18 系列的 BSP 要自己 cherry-pick。

### 5.2 Yocto 整合

把 cherry-pick 的 commit 匯出成 patch，在 BSP layer 的 kernel bbappend 加入：

```text
recipes-kernel/linux/
├── linux-imx_6.12.bbappend
└── linux-imx/0001-tpm-tpm_tis_spi-fix-nodef-CR50-tpm_tis_spi_resume-is.patch
```

```text
SRC_URI:append = " file://0001-tpm-tpm_tis_spi-fix-nodef-CR50-tpm_tis_spi_resume-is.patch"
```

之後如果 kernel 分支本身已經帶入這個 commit，要記得把這行和 patch 檔拿掉，不然 `do_patch` 會因為重複套用而失敗。

### 5.3 其他做法

- **開 `CONFIG_TCG_TIS_SPI_CR50=y`**：可以讓真正的 `tpm_tis_spi_resume()` 編進去，不用改程式。但這會多編一個用不到的 Cr50 driver，意圖也不清楚，這次沒有採用，也沒有實測。
- **resume 後重新綁定 driver**：放一個 systemd sleep hook，在 resume 後執行 unbind / bind。可以當作 kernel 修好之前的暫時做法，但 TPM 中斷、hwrng 等都會跟著重建，不建議長期使用。

### 5.4 驗證

把帶入 patch 的 image 燒進板子，一邊持續送 TPM 指令，一邊用電源鍵反覆 suspend / resume：

| | 修改前 | 修改後 |
|---|---|---|
| suspend / resume 次數 | 2 | 6 |
| resume 後 TPM 可用 | 0 | 6 |
| suspend 失敗（`/sys/power/suspend_stats/fail`） | 0 | 0 |
| resume 後的 SPI 傳輸量 | 0 | 約 11000 ~ 12000 次/秒（持續 TPM 指令下） |

每次 resume 之後 TPM 指令都能馬上成功，背景的測試迴圈也持續執行，沒有被打斷。網路介面要約 40 秒才重新拿到 DHCP 位址，這是 resume 後的正常行為，與本問題無關。

## 6. 相關但獨立的問題

追這個問題時，同一片板子上還發現兩個 TPM 相關的問題。它們和 suspend/resume 無關，另外處理：

- **TPM PIRQ 中斷拿不到**：SPL 沿用 NXP EVK 的 XSPI NOR flash reset 程式，把 TPM 的中斷腳設成輸出，kernel 只好改用 polling（`TPM interrupt not working, polling instead`）。
- **冷開機時 probe 讀到 `device-id 0xFFFF`**：SPI SCK 腳設成 pull-up，但 TPM 用 SPI mode 0。SoC 剛 reset 後，第一次 SPI 傳輸會多出一個 clock 邊緣，讀錯 ID；之後的存取都正常。

## 7. 結論與建議

1. **「resume 後 TPM 壞掉」不一定是硬體斷電。** 這次 TPM 一直有電、狀態也在，是 kernel 從來沒有通知 driver 已經 resume。
2. **先看 SPI 傳輸統計。** `/sys/bus/spi/devices/<dev>/statistics/transfers` 不會增加，就代表問題在 kernel 上層，不必去量 SPI 波形或懷疑 TPM。
3. **用 `TPM error (256)` 判斷 TPM 有沒有被 reset。** 重新綁定時沒有出現，表示 TPM 狀態有保留，可以排除電源和 reset 的方向。
4. **檢查 PM ops 是否成對。** 有 suspend callback 的 driver，resume callback 不能是 `NULL`，特別是被 `#if` 包起來的時候。
5. **上游修正沒有 backport。** 用 6.12 或 6.18 kernel 的 SPI TPM 產品都會遇到，BSP 要自己 cherry-pick `b4d1adc70471`。
6. **產品影響。** 只要系統允許 suspend（例如電源鍵設成 suspend），TPM 在第一次 resume 後就不能用，之後依賴 TPM 的功能（量測開機、金鑰、磁碟加密解鎖等）都會失敗，直到重開機。

## 附錄

### A. 常用指令

```bash
zcat /proc/config.gz | grep -E 'TCG_TIS_SPI|PM_SLEEP'
cat /sys/power/mem_sleep
dmesg | grep -E 'PM: suspend|tpm'

tpm2_getrandom --hex 8
cat /sys/bus/spi/devices/spi0.0/statistics/{transfers,errors,timedout}

echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/unbind
echo spi0.0 > /sys/bus/spi/drivers/tpm_tis_spi/bind

grep tpm /proc/interrupts
cat /sys/power/suspend_stats/fail
```

### B. 參考資料

- Linux：`drivers/char/tpm/tpm_tis_spi.h`、`tpm_tis_spi_main.c`、`tpm_tis_spi_cr50.c`
- Linux：`drivers/char/tpm/tpm-interface.c`（`tpm_pm_suspend` / `tpm_pm_resume`）
- Linux：`drivers/char/tpm/tpm-chip.c`（`tpm_try_get_ops`）
- Linux：`drivers/char/tpm/tpm_tis_core.c`（`tpm_tis_resume`）
- Linux commit `b4d1adc70471`：tpm: tpm_tis_spi: fix nodef CR50 tpm_tis_spi_resume is null
