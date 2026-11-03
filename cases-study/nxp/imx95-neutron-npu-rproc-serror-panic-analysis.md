# i.MX95 Neutron NPU 載入 firmware 時 SError kernel panic 分析（RESETCTRL 被 firmware 改寫）

## 1. 問題概述

一片 i.MX95 SMARC 自製板在 burn-in 測試中偶爾 kernel panic。每次都發生在 NPU 測項執行 `label_image` 開啟 `/dev/neutron0` 的時候，panic 的位置都在 Neutron remoteproc driver 把 firmware 寫進 NPU TCM 的過程：

```text
SError Interrupt on CPU5, code 0x00000000be000011 -- SError
Kernel panic - not syncing: Asynchronous SError Interrupt
 neutron_rproc_da_to_va+0xb0/0x10c
 rproc_elf_load_segments+0xbc/0x208
 rproc_start+0x30/0x168
 rproc_boot+0x510/0x528
 neutron_rproc_boot+0x5c/0x184
 neutron_open+0x74/0x1c0
```

已知的現象：

- 只在 burn-in 這類全負載測試中發生，第一次出現在開機約 13 分鐘後，也有開機 6 分鐘、1 小時才出現的。
- 只跑 NPU 推論、或只反覆開關 `/dev/neutron0`，跑 30 分鐘都沒有發生。
- panic 前 `dmesg` 沒有任何 NPU、clock 或 power domain 的錯誤。
- panic 之後板子停在 `SMP: stopping secondary CPUs`，即使設了 `kernel.panic=10` 也不會自動重開。

一開始的判斷是「NPU 在 power domain 或 clock 還沒準備好時被存取」。實際分析後，電源和 clock 都沒有問題：**NPU firmware 會自己改寫 Neutron 的 reset control 暫存器（RESETCTRL），舊版 driver 把讀回值不同當成啟動失敗，接著在 RESETCTRL 不允許 CPU 存取 TCM 的狀態下重新載入 firmware，寫入 TCM 就觸發 SError。** NXP 新版 BSP 已經有兩個對應的修正。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55，eIQ Neutron NPU） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP LF_6.12.34_2.1.0（Yocto 5.2，fsl-imx-wayland） |
| Kernel | 6.12.34（linux-imx lts-next），driver 編進 kernel（`CONFIG_NEUTRON=y`） |
| Driver | `drivers/remoteproc/imx_neutron_rproc.c`、`drivers/staging/neutron/` |
| NPU firmware | `NeutronFirmware.elf`（66896 bytes） |
| 推論程式 | TensorFlow Lite 2.19.0 `label_image` + `libneutron_delegate.so` |
| 負載工具 | `stress-ng`、`memtester`、`glmark2-es2-wayland`、VPU 解碼迴圈 |

Device tree 與 driver 中跟這次問題有關的設定：

| 項目 | 值 |
|---|---|
| Neutron remoteproc 節點 | `reg = <0x4ab00000 0x4>`，`power-domains = IMX95_PD_NPU` |
| RESETCTRL（driver 叫 SRC / SCR） | `0x4AB00000`，start 值 `0x1F11`，stop 值 `0x1F00` |
| DTCM | da `0x40000` → `0x4AB08000`（32 KB） |
| ITCM | da `0x0` → `0x4AB10000`（64 KB） |
| Runtime PM autosuspend | 1 秒（`NEUTRON_AUTOSUSPEND_DELAY`） |

## 3. 除錯過程

### 3.1 panic 的共同點

前後抓到 4 次 panic：

| 次數 | 開機時間 | CPU | pc | 當時的負載 |
|---|---|---|---|---|
| 1 | 802 s | 5 | `neutron_rproc_da_to_va+0xb0` | 完整 burn-in |
| 2 | 1557 s | 5 | `+0x8` | burn-in 開始約 2 分鐘 |
| 3 | 357 s | 0 | `+0x74` | burn-in |
| 4 | 3760 s | 0 | `+0xc8` | 加了診斷 log 的 kernel + 壓測腳本 |

共同點：

- 呼叫路徑都是 `neutron_open()` → `neutron_rproc_boot()` → `rproc_boot()` → `rproc_elf_load_segments()`。
- 從暫存器可以看出，出事的都是第一個 ITCM segment：da `0x80`、長度 `0x67c8`，寫到實體位址 `0x4AB10080`。
- pc 每次落在 `neutron_rproc_da_to_va()` 的不同位置。SError 是非同步例外，回報的 pc 只代表「例外送到 CPU 時正在執行的指令」，真正出錯的是前面對 ITCM 的寫入。

### 3.2 NPU 測項實際在做什麼

測項用 NXP 映像檔附的 `mobilenet_v1_1.0_224_quant.tflite`：

```bash
label_image -m mobilenet_v1_1.0_224_quant.tflite -i grace_hopper.bmp -l labels.txt \
    --external_delegate_path=/usr/lib/libneutron_delegate.so
```

log 顯示 `NeutronDelegate delegate: 0 nodes delegated out of 31 nodes`。這個模型沒有經過 Neutron converter 轉換，推論其實全部在 CPU 上跑。但每次執行還是會開啟 `/dev/neutron0`，讓 NPU 上電並載入 firmware。

burn-in 每隔一秒多跑一次推論，間隔比 autosuspend 的 1 秒長，所以每一次推論都是一次完整的「斷電 → 上電 → 載入 firmware」。

### 3.3 讀 driver 的電源管理流程

```text
neutron_open()
├─ pm_runtime_resume_and_get()      回傳值沒有檢查
│  └─ neutron_runtime_resume()
│     ├─ clk_bulk_prepare_enable()
│     └─ neutron_rproc_boot()       載入 firmware、啟動 NPU
└─ 如果 firmware 沒在跑，再呼叫一次 neutron_rproc_boot()

閒置 1 秒後
neutron_runtime_suspend()
├─ neutron_rproc_shutdown()
└─ clk_bulk_disable_unprepare()     genpd 接著關掉 PD_NPU
```

`rproc_boot()` 的順序是先 `rproc_elf_load_segments()`（用 `memcpy_toio` 把 firmware 寫進 TCM），再呼叫 `ops->start()` 寫 RESETCTRL 讓 NPU 開始執行。

當時列出三個嫌疑：

1. `neutron_open()` 沒有檢查 `pm_runtime_resume_and_get()` 的回傳值，上電失敗也會繼續往下走。
2. `neutron_runtime_resume()` 沒有處理 `clk_bulk_prepare_enable()` 失敗。
3. RESETCTRL 只在 probe 時（`neutron_rproc_addr_init()`）寫過一次 stop 值，之後重新上電再載入 firmware 時沒有再設定。

另外確認 NPU 的 `power_mode` 是 0（AUTO），TCM clock gating 只在 `power_mode=2` 才會啟用，所以排除「TCM clock 被關掉」。

### 3.4 第一個重現程式：只開關 /dev/neutron0

寫一支腳本每隔 0.9 ~ 1.5 秒開關一次 `/dev/neutron0`，同時跑 `stress-ng --cpu 6 --vm 2 --vm-bytes 1G --io 2`，並記錄 runtime PM 狀態和 `pm_genpd_summary` 裡 NPU power domain 的狀態。

| 時長 | 開啟次數 | power domain 開關 | 結果 |
|---|---|---|---|
| 30 分鐘 | 1406 | 約 700 次 | 0 次 panic，沒有 SCMI 或 clock 錯誤 |

單純開關裝置重現不出來。不過從 panic 的呼叫路徑可以推論：firmware 是在 `neutron_open()` 裡被載入，而不是在 runtime resume 裡，代表 resume 那一次的 boot 已經失敗了。

### 3.5 加診斷 log，壓測時抓到 panic 前的錯誤

修改 driver 加入診斷 log：

- 在 open、read、ioctl、hw_reset 檢查 `pm_runtime_resume_and_get()` 的回傳值，失敗時印 `open: failed to power up`。
- resume 時 clock 開啟失敗就回傳錯誤。
- resume 時 firmware 啟動失敗印 `resume: failed to boot firmware`，open 時發現 firmware 沒在跑也印出 runtime PM 狀態。

壓測改成更接近 burn-in：每 1.1 ~ 1.6 秒跑一次 `label_image -c 1`，背景同時跑 `stress-ng --cpu 4 --vm 2 --vm-bytes 15%`、`memtester 4000M`、`glmark2-es2-wayland --off-screen` 和 VPU 解碼迴圈，並把 NPU 的 `power/control` 設成 `auto`。

約 60 分鐘、約 1800 次上電後 panic，serial console 抓到 panic 前的 log：

```text
[3760.128061] remoteproc remoteproc0: powering up neutron-rproc
[3760.138990] remoteproc remoteproc0: Booting fw image NeutronFirmware.elf, size 66896
[3760.156185] remoteproc remoteproc0: can't start rproc neutron-rproc: -22
[3760.172123] neutron 4ab00004.imx95-neutron: could not boot a remote processor
[3760.180117] neutron 4ab00004.imx95-neutron: resume: failed to boot firmware: -22
[3760.187917] neutron 4ab00004.imx95-neutron: open: firmware not running after resume (rpm 0, usage 1)
[3760.197779] remoteproc remoteproc0: powering up neutron-rproc
[3760.204639] remoteproc remoteproc0: Booting fw image NeutronFirmware.elf, size 66896
[3760.212909] SError Interrupt on CPU0, code 0x00000000be000011 -- SError
```

這段 log 推翻了「上電失敗」的猜測：

- 沒有出現 `open: failed to power up`，`rpm 0` 代表裝置是 active，power domain 和 clock 都正常。
- 真正失敗的是第一次 boot 的 `ops->start()`，`neutron_rproc_start()` 回傳 `-EINVAL`。
- `neutron_open()` 接著再 boot 一次，就在第二次載入 firmware 時觸發 SError。

當時的 `neutron_rproc_start()`：

```c
static int neutron_rproc_start(struct rproc *rproc)
{
	struct neutron_rproc *priv = rproc->priv;
	const struct imx_rproc_dcfg *dcfg = priv->dcfg;

	writel(dcfg->src_start, priv->regbase + dcfg->src_reg);

	if (readl(priv->regbase + dcfg->src_reg) != dcfg->src_start)
		return -EINVAL;

	return 0;
}
```

唯一會回傳 `-EINVAL` 的地方，就是寫入 start 值後讀回來不一樣。

### 3.6 印出讀回值，並在失敗時先恢復 stop 值

再改一版診斷 kernel：讀回值不符時印出寫入值和讀回值，100 µs 後再讀一次，然後把 RESETCTRL 寫回 stop 值。

| 測試 | 時長 | 推論次數 | 讀回不符 | panic |
|---|---|---|---|---|
| 第一輪 | 2 小時 | 3503 | 0 | 0 |
| 整晚 | 10 小時 | 17511 | 4 | 0 |

整晚那一輪抓到 4 次讀回不符，全部被救回來，沒有 SError：

```text
[20646.771379] neutron_rproc 4ab00000.imx95-neutron-remoteproc: start: SCR reads 0x1ff1, 0x1ff1 after 100us, wrote 0x1f11
[20646.790913] neutron_rproc 4ab00000.imx95-neutron-remoteproc: start: SCR put back to stop, reads 0x1f00
[20646.811295] remoteproc remoteproc0: can't start rproc neutron-rproc: -22
[20646.839294] neutron 4ab00004.imx95-neutron: resume: failed to boot firmware: -22
[20646.855314] neutron 4ab00004.imx95-neutron: open: firmware not running after resume (rpm 0, usage 1)
```

- 寫入 `0x1F11`，讀回 `0x1FF1`，多了 bit 5 ~ 7（`0xE0`），100 µs 後仍然一樣。
- 4 次分別發生在開始後 1 小時 29 分、1 小時 53 分、2 小時 50 分、8 小時 14 分，大約每 3 小時一次，也就是每 4000 ~ 5000 次上電一次。
- 寫回 stop 值後，下一次 boot 就正常了。

i.MX95 Reference Manual 的 Neutron 章節寫明「eIQ Neutron NPU does not have any user-configurable registers」，查不到 RESETCTRL 各個 bit 的定義，所以只能從行為推論 bit 5 ~ 7 是 firmware 自己設定的。

### 3.7 對照 NXP 新版 BSP

比對 NXP 新版 kernel 的 `imx_neutron_rproc.c`，找到兩個正好對應的修正（內容見 §5）：

- LF-16577：commit message 寫明 firmware 會為了省電主動管理 SRC 暫存器，寫入後可能被改掉，讀回比對不可靠。
- LF-18145：commit message 寫明上電後 RESETCTRL 的預設值不允許存取 Neutron memory，載入 ELF 前要先設定。

## 4. Root Cause 分析

### 4.1 出事的時序

```text
推論結束 1 秒後
runtime suspend：rproc shutdown、關 clock、PD_NPU 斷電

下一次推論 open /dev/neutron0
runtime resume
├─ PD_NPU 上電，RESETCTRL 回到上電預設值
├─ rproc_elf_load_segments()    第一次載入：TCM 寫入正常
└─ neutron_rproc_start()
   ├─ 寫入 0x1F11
   └─ firmware 已經把它改成 0x1FF1 → 讀回不符 → -EINVAL     ← 約每 4000 ~ 5000 次一次

neutron_open() 發現 firmware 沒在跑，再 boot 一次
└─ rproc_elf_load_segments()
   └─ memcpy_toio() 寫 ITCM 0x4AB10080
      RESETCTRL 還是 0x1FF1，不是允許 CPU 存取 TCM 的 stop 值
      → bus error → SError → kernel panic
```

### 4.2 為什麼是 SError，pc 又不固定

對 TCM 的寫入是 posted write，CPU 不會等寫入完成。被拒絕的寫入在 bus 上回報錯誤時，CPU 已經往下執行好幾道指令，所以收到的是非同步的 SError（`code 0x00000000be000011`），回報的 pc 落在 `neutron_rproc_da_to_va()` 的不同位置，不是真正出錯的那道寫入。在 kernel 裡發生的 SError 無法恢復，只能 panic。

### 4.3 為什麼要 burn-in 才會發生

兩個條件同時成立才會出事：

1. firmware 改寫 RESETCTRL 的機率很低，大約每 4000 ~ 5000 次上電一次。
2. 推論間隔要比 1 秒的 autosuspend 長，每次推論才會讓 NPU 完整斷電、上電、重新載入 firmware。

burn-in 每隔一秒多跑一次推論，一小時就有約 2000 次上電，所以跑幾個小時就很容易遇到。只開關 `/dev/neutron0` 的重現程式雖然也有上電，但沒有讓 firmware 真正執行推論，30 分鐘的次數也不夠。

### 4.4 早期嫌疑的結論

| 嫌疑 | 結論 |
|---|---|
| 1. 沒檢查 `pm_runtime_resume_and_get()` 回傳值 | 不是原因，出事時 runtime PM 是 active |
| 2. resume 時沒處理 clock 開啟失敗 | 不是原因，沒有 clock 錯誤 |
| 3. RESETCTRL 只在 probe 時設定 | 最接近：載入 firmware 前沒有把 RESETCTRL 設成允許存取 TCM 的值 |

## 5. 解決方案

### 5.1 LF-16577：拿掉 start / stop 的讀回比對

NXP commit `f96d40368101`（"LF-16577 remoteproc: imx_neutron: Remove register write verification in start/stop"）：

```diff
@@ -85,9 +85,6 @@ static int neutron_rproc_start(struct rproc *rproc)
 
 	writel(dcfg->src_start, priv->regbase + dcfg->src_reg);
 
-	if (readl(priv->regbase + dcfg->src_reg) != dcfg->src_start)
-		return -EINVAL;
-
 	return 0;
 }
 
@@ -98,9 +95,6 @@ static int neutron_rproc_stop(struct rproc *rproc)
 
 	writel(dcfg->src_stop, priv->regbase + dcfg->src_reg);
 
-	if (readl(priv->regbase + dcfg->src_reg) != dcfg->src_stop)
-		return -EINVAL;
-
 	return 0;
 }
```

firmware 改寫 RESETCTRL 不再被當成啟動失敗，`neutron_open()` 就不會走到第二次載入。

### 5.2 LF-18145：載入 ELF 前先設定 RESETCTRL

NXP commit `e6329ca071b1`（"LF-18145 remoteproc: imx_neutron: enable dtcm memory access before ELF load"）新增 `.prepare` callback。`rproc_boot()` 在載入 ELF 之前會先呼叫它：

```c
static int neutron_rproc_prepare(struct rproc *rproc)
{
	struct neutron_rproc *priv = rproc->priv;
	const struct imx_rproc_dcfg *dcfg = priv->dcfg;
	/*
	 * After first power-on, default RESETCTRL settings restrict access,
	 * preventing Cortex-A from accessing Neutron DTCM memory.
	 * Configure the register here to enable Neutron memory access before
	 * remoteproc copies ELF segments into Neutron DTCM.
	 */
	writel(dcfg->src_stop, priv->regbase + dcfg->src_reg);

	return 0;
}

static const struct rproc_ops neutron_rproc_ops = {
	.prepare	= neutron_rproc_prepare,
	.start		= neutron_rproc_start,
	...
};
```

每次載入 firmware 前都把 RESETCTRL 設成 stop 值，不管 RESETCTRL 先前被誰改成什麼，CPU 都能存取 TCM。這是根本的修正；LF-16577 則避免不必要的重新載入。

### 5.3 整合

兩個 commit 用 `git cherry-pick -x` 直接套到 6.12.34 的 kernel，沒有衝突。

NXP 6.18 的 kernel 另外有一個 commit 拿掉了 probe 時寫 RESETCTRL 的動作（`neutron_rproc_addr_init()`）。有了 `.prepare` 之後 probe 時的寫入已經多餘，但留著也無害，所以沒有一起帶入。

### 5.4 驗證

用 §3.5 的壓測腳本（推論間隔 1.1 ~ 1.6 秒，CPU、記憶體、GPU、VPU 同時加壓，`power/control=auto`）比較：

| Kernel | 時長 | 推論次數 | 推論失敗 | panic |
|---|---|---|---|---|
| 原始 6.12.34（burn-in） | 開機 6 ~ 26 分鐘 | — | — | 3 次（§3.1 第 1 ~ 3 次） |
| 診斷版（只加 log） | 約 1 小時 | 約 1800 | — | 1 次（§3.5） |
| 診斷版（失敗時寫回 stop 值） | 10 小時 | 17511 | 0 | 0（讀回不符 4 次，全部救回） |
| 套用 LF-16577 + LF-18145 | 10 小時 | 18005 | 0 | 0 |

套用修正後跑 10 小時，系統負載平均約 10，板子連續運作沒有重開，`dmesg` 也沒有任何 start 或 firmware 錯誤。以修正前每 3 小時一次的發生率估算，同樣 10 小時原始 kernel 預期會 panic 3 ~ 4 次。

## 6. 上游狀態

截至 2026-11 查詢的結果：

| 來源 | LF-16577（拿掉讀回比對） | LF-18145（`.prepare`） |
|---|---|---|
| NXP LF_6.12.34_2.1.0 | ✗ | ✗ |
| nxp-imx/linux-imx `lf-6.12.y` | ✓（程式已無讀回比對） | ✗ |
| NXP LF_6.18.2_1.0.0 | ✓ | ✗ |
| NXP LF_6.18.20_2.0.0 之後、`lf-6.18.y` | ✓ | ✓ |
| Linux mainline | Neutron remoteproc driver 不在 mainline | — |

使用 6.12 系列 BSP 的產品，要自己帶入這兩個 commit。只帶 LF-16577 雖然能大幅降低發生機率，但只要 RESETCTRL 在載入前不是 stop 值就還是可能出事，建議兩個一起帶。

## 7. 結論與建議

1. **SError 的 pc 不可信。** 非同步 SError 回報的 pc 只是例外送達時的位置，要從暫存器內容（寫入的位址、長度）和呼叫路徑判斷真正的存取。
2. **先證實再修。** 一開始懷疑的「上電失敗」很合理，但加了診斷 log 之後證明 runtime PM 是 active。讀回值 `0x1FF1` 這個證據，才讓問題指向 firmware 改寫暫存器。
3. **低機率問題要算次數。** 每 4000 ~ 5000 次上電才一次，30 分鐘的重現程式跑不出來。壓測要讓每次推論都完整斷電再上電（推論間隔大於 autosuspend 的 1 秒），並且至少跑 10 小時。
4. **panic 要靠 serial console。** 這次 panic 後板子停住，`kernel.panic` 不會重開，`dmesg` 也留不下來。壓測時要接 debug console，並在開機後提高 console 的 loglevel（`dmesg -n 7`），否則 panic 前的 log 會漏掉。
5. **NPU 測項要用轉換過的模型。** 附帶的 mobilenet 沒有任何 node 交給 Neutron，推論全在 CPU 上跑，只能測到「NPU 上電與 firmware 載入」，測不到 NPU 的運算。燒機測試應該改用 Neutron converter 轉換過的模型。
6. **driver 仍值得補強。** `neutron_open()` 不檢查 `pm_runtime_resume_and_get()` 的回傳值，雖然不是這次的原因，但上電失敗時會繼續存取硬體，建議另外補上。

## 附錄

### A. 壓測腳本（簡化版）

```bash
#!/usr/bin/env bash
# usage: npu_stress.sh <seconds>
dur=${1:-36000}
echo auto > /sys/bus/platform/devices/4ab00004.imx95-neutron/power/control
sysctl -qw kernel.panic=10

stress-ng --cpu 4 --vm 2 --vm-bytes 15% --timeout "${dur}" &
memtester 4000M >/dev/null &

cd /usr/bin/tensorflow-lite-*/examples || exit 1
end=$((SECONDS + dur)) run=0 fail=0
while ((SECONDS < end)); do
	if label_image -c 1 -m mobilenet_v1_1.0_224_quant.tflite -i grace_hopper.bmp -l labels.txt \
		--external_delegate_path=/usr/lib/libneutron_delegate.so 2>&1 | grep -q "military uniform"; then
		run=$((run + 1))
	else
		fail=$((fail + 1))
	fi
	echo "$(date +%T) up=$(cut -d' ' -f1 /proc/uptime) run=${run} fail=${fail}"
	sleep "1.$((RANDOM % 6 + 1))"
done
kill $(jobs -p) 2>/dev/null
```

GPU（`glmark2-es2-wayland --off-screen`）和 VPU 解碼迴圈可以依需要另外加在背景。

### B. 常用指令

| 用途 | 指令 |
|---|---|
| 看 NPU firmware 狀態 | `cat /sys/class/remoteproc/remoteproc0/state` |
| 看 runtime PM 狀態 | `cat /sys/bus/platform/devices/4ab00004.imx95-neutron/power/runtime_status` |
| 看 NPU power domain | `grep -i npu /sys/kernel/debug/pm_genpd/pm_genpd_summary` |
| 讓 NPU 不進 runtime suspend | `echo on > /sys/bus/platform/devices/4ab00004.imx95-neutron/power/control` |
| 提高 console loglevel | `dmesg -n 7` |
| 看推論有沒有交給 NPU | `label_image ... 2>&1 \| grep "nodes delegated"` |

### C. 參考資料

- nxp-imx/linux-imx commit `f96d40368101`：LF-16577 remoteproc: imx_neutron: Remove register write verification in start/stop
- nxp-imx/linux-imx commit `e6329ca071b1`：LF-18145 remoteproc: imx_neutron: enable dtcm memory access before ELF load
- i.MX95 Applications Processor Reference Manual，eIQ Neutron NPU 章節
