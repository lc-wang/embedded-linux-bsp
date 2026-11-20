# i.MX95 Android 關機後 SoM 仍有電分析（ATF 預設只關 AP logical machine）

## 1. 問題概述

一片 i.MX95 SMARC 自製板移植到 Android 16 後，做 power on / off 測項時發現：從 Android 的電源選單點「關機」，螢幕關了、console 也印出 `reboot: Power down`，但 **SoM 上的 LED 還是亮著**。長按 power key 強制斷電時，LED 則會熄滅。

已知的現象：

- 選單關機的流程本身正常：init 卸載 /data、/metadata 後交給 kernel，`powerctl_shutdown_time_ms` 約 4～8.5 秒。
- 關機後按 power key 可以正常開機，SPL 印出 `SYS Boot reason: bbm`。
- 長按 power key 直到斷電：console 沒有任何輸出，LED 熄滅，再按 power key 一樣可以開機。
- 兩種關機方式，對使用者來說都是「關機」，但 LED 的狀態不同。

一開始懷疑是 LED 的接線或 PMIC 的設定。實際原因在軟體：**i.MX95 的 ATF 預設在 PSCI SYSTEM_OFF 時，只請 System Manager 關掉 A55（AP）這個 logical machine。System Manager 和 PMIC 都沒有關，SoM 一直有電。** 這是 NXP 的預設行為，不是這片板子特有的問題。

## 2. 系統環境

| 項目 | 內容 |
|---|---|
| SoC | NXP i.MX95（6 核 Cortex-A55 + Cortex-M33 System Manager + Cortex-M7） |
| 板子 | i.MX95 SMARC 模組 + 自製載板 |
| BSP | NXP android-16.0.0_2.0.0 |
| ATF | `vendor/nxp-opensource/arm-trusted-firmware`，v2.14.1，`PLAT=imx95` |
| System Manager | `vendor/nxp-opensource/imx-sm`，板子設定 `configs/smarc-imx95.cfg` |
| PMIC | PF09 + PF5301 + PF5302 |
| 建置 | `device/nxp/imx9/<board>/AndroidUboot.sh` 編 ATF（bl31）並打包進 bootloader |

System Manager 設定檔裡的 logical machine（LM）與 SCMI agent：

| 名稱 | 角色 | 相關設定 |
|---|---|---|
| LM0 `SM` | System Manager 本身（M33） | `boot=1` |
| LM1 `M7` | Cortex-M7 | `boot=2, skip=1`（預設不啟動） |
| LM2 `AP` | Cortex-A55（ATF + Linux / Android） | `default` |
| SCMI agent `AP-S` | ATF（secure） | `SYS ALL` |
| SCMI agent `AP-NS` | Linux（non-secure） | `SYS NOTIFY` |

## 3. 除錯過程

### 3.1 用 debug console 比對兩種關機

在 debug console（加上時間戳）記錄每一次關機和開機：

| 方式 | console 最後的輸出 | LED | 再按 power key |
|---|---|---|---|
| 選單關機 | `init: powerctl_shutdown_time_ms:5461:0` → `reboot: Power down` | 亮 | 開機，`SYS Boot reason: bbm` |
| 長按強制斷電 | 沒有輸出 | 熄 | 開機，`SYS Boot reason: bbm` |
| 拔插 DC 電源 | — | — | 自動開機，`SYS Boot reason: por` |

選單關機時 Linux 確實走完 power off。`reboot: Power down` 之後，LED 還亮著，表示斷電的動作沒有傳到 PMIC。

### 3.2 追 power off 的路徑

```text
Android 選單「關機」
└─ init: sys.powerctl=shutdown
   └─ kernel: kernel_power_off()
      └─ PSCI SYSTEM_OFF（SMC 進 ATF）
         └─ ATF imx_system_off()
            └─ SCMI SYSTEM_POWER_STATE_SET（AP-S agent → System Manager）
               └─ SM：依 state 決定只關 LM，或整個系統關機
```

ATF：`plat/imx/imx9/common/imx9_psci_common.c`

```c
#if SYS_PWR_FULL_CTRL == 1
#define PLAT_SCMI_SYS_PWR_SHUTDOWN	IMX9_SCMI_SYS_PWR_FULL_SHUTDOWN   /* 0x80000001 */
#define PLAT_SCMI_SYS_PWR_COLD_RESET	IMX9_SCMI_SYS_PWR_FULL_RESET
#else
#define PLAT_SCMI_SYS_PWR_SHUTDOWN	SCMI_SYS_PWR_SHUTDOWN             /* 0x0 */
#define PLAT_SCMI_SYS_PWR_COLD_RESET	SCMI_SYS_PWR_COLD_RESET
#endif

void imx_system_off(void)
{
	ret = scmi_sys_pwr_state_set(imx9_scmi_handle,
				     SCMI_SYS_PWR_FORCEFUL_REQ,
				     PLAT_SCMI_SYS_PWR_SHUTDOWN);
	...
}
```

`plat/imx/imx9/imx95/platform.mk`：

```make
SYS_PWR_FULL_CTRL ?= 0
```

Android 的 `AndroidUboot.sh` 編 ATF 時沒有帶這個變數，所以用的是預設值 0。

### 3.3 System Manager 收到之後做什麼

`imx-sm/sm/rpc/scmi/rpc_scmi_sys.c` 的 `SystemPowerUpdate()`：

```c
    switch (systemState)
    {
        case SYS_STATE_SHUTDOWN:
            /* Shutdown */
            status = LMM_SystemLmShutdown(caller->lmId, caller->instAgentId,
                lmId, graceful, &g_swReason);
            break;
        ...
        case SYS_STATE_FULL_SHUTDOWN:
            /* System shutdown */
            status = LMM_SystemShutdown(caller->lmId,
                caller->instAgentId, graceful, &g_swReason);
            break;
```

`SYS_STATE_FULL_SHUTDOWN` 最後會呼叫 `devices/MIMX95/sm/dev_sm_system.c`：

```c
int32_t DEV_SM_SystemShutdown(void)
{
    /* Inform ELE */
    ELE_StartDvfsChange(ELE_DVFS_FLAG_BBSM, 0U, 0U, 0U);

    /* Request shutdown */
    BBNSM_SystemPowerOff(BBNSM);
    ...
}
```

所以：

- **state = 0（SHUTDOWN）**：只關呼叫者所在的 LM，也就是 AP。System Manager（M33）繼續跑，PMIC 不受影響。
- **state = 0x80000001（FULL_SHUTDOWN）**：System Manager 叫 BBNSM 關機，PMIC 關掉所有電源。

### 3.4 為什麼關了 AP 還能用 power key 開機

AP 被關掉之後，System Manager 還在跑。按下 power key 時，BBNSM 的按鍵事件送到 System Manager，System Manager 再把 AP 這個 LM 開起來，所以 SPL 一樣印出 `bbm`。從使用者的角度看，關機、開機都正常，只有「SoM 還有電」這點看得出差別。

## 4. Root Cause 分析

### 4.1 背景與術語

| 術語 | 白話 |
|---|---|
| System Manager（SM） | i.MX95 上跑在 Cortex-M33 的韌體，管理電源、時脈、reset，其他核心透過 SCMI 跟它要資源 |
| Logical Machine（LM） | SM 把 SoC 切成幾個獨立的「機器」（這裡是 SM、M7、AP），每個 LM 可以單獨開關、重置 |
| SCMI SYSTEM_POWER_STATE_SET | 請 SM 改變系統電源狀態的指令，參數決定只影響自己的 LM，還是整個系統 |
| BBNSM | Battery-Backed Non-Secure Module，管 ONOFF 按鍵、RTC，以及送給 PMIC 的 `PMIC_ON_REQ` |
| bbm / por | SPL 印出的開機原因：`bbm` = BBNSM 事件（按鍵），`por` = 上電重置 |

### 4.2 根因

```text
SYS_PWR_FULL_CTRL = 0（預設）
  Linux power off → PSCI SYSTEM_OFF → ATF
  ATF 送 SCMI_SYS_PWR_SHUTDOWN (0)
  SM: LMM_SystemLmShutdown → 只關 LM2 (AP)
  SM、PMIC 繼續供電                  → SoM LED 亮

SYS_PWR_FULL_CTRL = 1
  ATF 送 IMX9_SCMI_SYS_PWR_FULL_SHUTDOWN (0x80000001)
  SM: LMM_SystemShutdown → DEV_SM_SystemShutdown → BBNSM_SystemPowerOff
  PMIC 關閉                          → SoM LED 熄（跟長按強制斷電相同）
```

事實整理：

1. i.MX95 是多個 LM 的架構，NXP 預設讓 AP「只關自己」，以免影響其他還在跑的 LM（例如 M7 上的即時任務）。
2. 這片板子的 M7 LM 設定為 `skip=1`，沒有在用，所以只關 AP 沒有意義，反而讓關機後 SoM 一直耗電。
3. 同一個 flag 也會把 reboot 從「只重置 AP」改成「整個系統 reset」（`IMX9_SCMI_SYS_PWR_FULL_RESET`），要一起驗證。
4. Yocto 的 meta-imx（`imx-atf_2.14.bb`）和自製 BSP layer 都沒有設定 `SYS_PWR_FULL_CTRL`，預設行為應該相同，但這次沒有在 Yocto 上實測。

### 4.3 權限

非 graceful 的 FULL_SHUTDOWN，SM 要求呼叫者有 `SM_SCMI_PERM_EXCLUSIVE` 以上的 SYS 權限。ATF 用的 agent `AP-S` 是 `SYS ALL`，所以可以下這個請求；Linux 的 `AP-NS` 只有 `SYS NOTIFY`，不能直接下。實測 SM 沒有拒絕（ATF 沒有印出 `imx_system_off failed`）。

## 5. 解決方案

### 5.1 修正 patch

在板子的 `AndroidUboot.sh` 編 ATF 時加上 `SYS_PWR_FULL_CTRL=1`（trusty 和非 trusty 兩行都要加）：

```diff
-		make -C ${IMX_PATH}/arm-trusted-firmware/ CROSS_COMPILE="${ATF_CROSS_COMPILE}" PLAT=`echo $2 | cut -d '-' -f1` bl31 -B SPD=trusty 1>/dev/null || exit 1
+		make -C ${IMX_PATH}/arm-trusted-firmware/ CROSS_COMPILE="${ATF_CROSS_COMPILE}" PLAT=`echo $2 | cut -d '-' -f1` bl31 -B SYS_PWR_FULL_CTRL=1 SPD=trusty 1>/dev/null || exit 1
 	else
 ...
-		make -C ${IMX_PATH}/arm-trusted-firmware/ CROSS_COMPILE="${ATF_CROSS_COMPILE}" PLAT=`echo $2 | cut -d '-' -f1` bl31 -B 1>/dev/null || exit 1
+		make -C ${IMX_PATH}/arm-trusted-firmware/ CROSS_COMPILE="${ATF_CROSS_COMPILE}" PLAT=`echo $2 | cut -d '-' -f1` bl31 -B SYS_PWR_FULL_CTRL=1 1>/dev/null || exit 1
```

只影響這塊板子，ATF 原始碼和其他板子都不用改。

確認有沒有生效：反組譯 `bl31.elf` 的 `imx_system_off`，傳給 `scmi_sys_pwr_state_set` 的第三個參數要是 `0x80000001`：

```text
<imx_system_off>:
  mov  w2, #-0x7fffffff        // = 0x80000001 (FULL_SHUTDOWN)
  ldr  x0, [x0, #0xaf8]
  mov  w1, #0x0                // FORCEFUL
  bl   <scmi_sys_pwr_state_set>
```

### 5.2 Yocto 的對應做法（未實測）

在自製的 BSP layer 加一個 `imx-atf_%.bbappend`，只針對這塊板子的 machine：

```bitbake
EXTRA_OEMAKE:append:<machine> = " SYS_PWR_FULL_CTRL=1"
```

驗證項目和 Android 相同。

### 5.3 考慮過但沒採用的做法

| 做法 | 問題 |
|---|---|
| 直接改 ATF 的 `platform.mk` 預設值 | 影響所有 i.MX95 板子，包括真的需要 M7 繼續跑的板子 |
| 在 SM 收到 AP 的 SHUTDOWN 時改成整機關機 | 改動 SM 的 LM 語意，維護成本高，且與 NXP 的設計不一致 |
| 維持現狀，在報告註明 | 功能上可以接受，但待機耗電較高，而且和長按關機的行為不一致 |

### 5.4 驗證結果

| 項目 | 方法 | 結果 |
|---|---|---|
| 選單關機（修正前） | 3 次，console 記錄 | 正常關機、正常開機，SoM LED 亮 |
| 長按強制斷電 | 2 次 | LED 熄；開機後 `boot_completed=1`，沒有新的 tombstone，f2fs 只有正常的 fsync 資料回復，沒有錯誤 |
| 選單關機（修正後） | console 記錄 + 目視 LED | `reboot: Power down` 後 **LED 熄**，沒有 `imx_system_off failed` |
| 修正後按 power key 開機 | console | `SYS Boot reason: bbm`，約 27 秒到桌面，`boot_completed=1` |
| 修正後 reboot | `su 0 reboot` | `reboot: Restarting system` → SPL → kernel，`boot_completed=1`，`sys.boot.reason=reboot,shell` |
| 拔插 DC 電源 | console | 自動開機，`SYS Boot reason: por`（cold boot 測項可以直接用定時插座） |

## 6. 上游狀態

- 這是 NXP 預設的設計（ATF `SYS_PWR_FULL_CTRL ?= 0`），不是 bug。NXP 的 Android 和 Yocto 都沒有替板子打開這個選項。
- 修正放在板子自己的建置腳本，不改 ATF 原始碼。

## 7. 結論與建議

1. **i.MX95 的「關機」有兩種層級。** 只關 AP 的 LM，或整個系統關機，由 ATF 的 `SYS_PWR_FULL_CTRL` 決定。新板子 bring-up 時，要依產品需求（M7 要不要在 AP 關機後繼續跑）明確選一種。
2. **`reboot: Power down` 不代表斷電了。** 判斷有沒有真的斷電，要看 PMIC 輸出（LED、量電流），或看下次開機的 boot reason。
3. **power on / off 測項要比對多種方式。** 這次是把選單關機和長按強制斷電放在一起比，才看出 LED 狀態不同。
4. **改了關機路徑，reboot 也要一起測。** 同一個 flag 也改變了 cold reset 的範圍。
5. **用 boot reason 分辨開機方式。** `bbm` 是按鍵，`por` 是上電。做 cold boot 測項時，可以用它確認每一次都是真的斷電再上電。

## 附錄

### A. 常用指令

```bash
# console 上看關機 / 開機
#   init: powerctl_shutdown_time_ms:...   Android 關機流程花費時間
#   reboot: Power down                     kernel 送出 power off
#   SYS Boot reason: bbm / por             SPL 印出的開機原因

getprop sys.boot.reason
getprop sys.boot_completed
logcat -b events -d | grep boot_progress_enable_screen

# 確認 bl31 的 power off 參數
llvm-objdump -d build/imx95/release/bl31/bl31.elf | grep -A8 "<imx_system_off>:"
```

### B. 參考資料

- ATF：`plat/imx/imx9/common/imx9_psci_common.c`（`imx_system_off`、`PLAT_SCMI_SYS_PWR_SHUTDOWN`）
- ATF：`plat/imx/imx9/imx95/platform.mk`（`SYS_PWR_FULL_CTRL`）
- ATF：`drivers/nxp/scmi/vendor/scmi_imx9.h`（`IMX9_SCMI_SYS_PWR_FULL_SHUTDOWN`）
- imx-sm：`sm/rpc/scmi/rpc_scmi_sys.c`（`SystemPowerStateSet`、`SystemPowerUpdate`）
- imx-sm：`devices/MIMX95/sm/dev_sm_system.c`（`DEV_SM_SystemShutdown`）
- imx-sm：`configs/<board>.cfg`（LM 與 SCMI agent 權限）
- `device/nxp/imx9/<board>/AndroidUboot.sh`（ATF 建置參數）
