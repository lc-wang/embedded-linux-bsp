

# BSP 層的 Suspend / Resume 機制 
這份筆記整理 Linux BSP 層在 suspend/resume 流程中的角色與設計要點，  
說明 SoC、Bootloader、Kernel、Driver 各層如何協同達成系統低功耗運作。

---
 
## 1. Suspend / Resume 分層架構

| 層級 | 功能角色 | 範例元件 |
| --- | --- | --- |
| **User Space** | systemd / PowerHAL 發出休眠請求 | `echo mem > /sys/power/state`、Android PowerHAL |
| **Kernel Power Management Core** | 管理裝置 suspend 順序、控制 runtime PM、統合 wakeup source | `kernel/power/` |
| **Device Drivers** | 實作 `.suspend()` / `.resume()` callback，保存與恢復裝置狀態 | I2C, SPI, Display, Audio drivers |
| **BSP / SoC Layer** | 控制時鐘、電源域、PMIC、DDR self-refresh | Clock / Regulator / PMIC / SCU |
| **Bootloader / Firmware** | 提供 early resume、DDR 初始化、secure entry | U-Boot SPL、ARM Trusted Firmware (ATF) |

--- 
## 2. Suspend 模式類型 | 模式 | 說明 | 電源狀態 |
| --- | --- | --- |
| **Freeze** | 只停用 user space，CPU idle；最輕量級 suspend | CPU 停止執行，RAM 保持 |
| **Suspend-to-RAM (mem)** | 進入深度睡眠，大多數裝置關閉 | CPU/外設斷電，DDR 自刷新 |
| **Hibernate (suspend-to-disk)** | 儲存系統狀態到磁碟，再關機 | 全部斷電，重新上電時恢復 |
| **Runtime PM** | 單一裝置級別的動態休眠 | 僅該裝置關閉電源 |

--- 

## 3. 系統 Suspend 流程（SoC 視角）

User space  
  ↓  
Kernel PM core  
  ↓  
各 device suspend() 依序執行  
  ↓  
關閉 peripheral clock / power domain  
  ↓  
SoC 進入低功耗模式（ARM WFI / WFE）  
  ↓  
等待 wakeup source 觸發  
  ↓  
恢復電源與時鐘  
  ↓  
各 device resume() 執行  
  ↓  
回到正常運作


## 4. BSP 層任務
BSP 層負責將 SoC 硬體的「低功耗能力」與 Kernel 的 PM framework 整合。

| 模組 | 任務 | 相關檔案 |
| --- | --- | --- |
| **Clock** | 關閉未使用的 clock，resume 時重新啟用 | `drivers/clk/` |
| **Power Domain (genpd)** | 控制 SoC 模組電源域 (VPU/GPU/ISP 等) | `drivers/base/power/domain.c` |
| **Regulator** | 控制 PMIC 電壓供應 | `drivers/regulator/` |
| **SCU / PMIC 通訊** | BSP 特定控制介面（如 I2C/IPC 寫入暫存器） | `arch/arm64/mach-*/pm.c` |
| **Wakeup Controller** | 管理可喚醒的中斷來源 | `drivers/base/power/wakeup.c` |

---

## 5. 驅動層 Callback 範例

```c
static int mydevice_suspend(struct device *dev)
{
    struct mydev *d = dev_get_drvdata(dev);

    disable_irq(d->irq);
    clk_disable_unprepare(d->clk);
    regulator_disable(d->vcc);
    pr_info("mydevice: suspended\n");

    return 0;
}

static int mydevice_resume(struct device *dev)
{
    struct mydev *d = dev_get_drvdata(dev);

    regulator_enable(d->vcc);
    clk_prepare_enable(d->clk);
    enable_irq(d->irq);
    pr_info("mydevice: resumed\n");

    return 0;
}

static const struct dev_pm_ops mydevice_pm_ops = {
    .suspend = mydevice_suspend,
    .resume  = mydevice_resume,
};

static struct platform_driver mydevice_driver = {
    .driver = {
        .name = "mydevice",
        .pm = &mydevice_pm_ops,
    },
};
```
----------

## 6. Wakeup Source (喚醒來源)

-   某些外設（例如 GPIO、USB、RTC、按鍵）能在系統 suspend 時喚醒系統。
-   啟用方法：
    `device_init_wakeup(dev, true);` 
-   在中斷中呼叫：
    `pm_wakeup_event(dev, 0);` 
-   查看目前喚醒統計：
    `cat /sys/kernel/debug/wakeup_sources` 
----------


## 7. Bootloader 的角色

在部分 SoC（特別是 ARM 平台）中，Bootloader 會參與早期的 suspend/resume 過程（early suspend/resume），  
主要負責安全層呼叫與記憶體重新初始化。

| 階段 | 功能 | 實作位置 |
| --- | --- | --- |
| **SPL / ATF** | 實作 Secure Monitor，提供低功耗入口（PSCI call）。 | ARM Trusted Firmware |
| **U-Boot** | 可在 resume 階段重新初始化 PMIC 或 DDR。 | `arch/arm/mach-*/lowlevel_init.S` |
| **Kernel** | 透過 PSCI 或 firmware 介面呼叫 SoC 低功耗函式。 | `drivers/firmware/psci/psci.c` |

---

### 常見接口
```c
psci_system_suspend();
psci_cpu_suspend();
```

----------


## 8. Debug 工具與節點

| 工具 / 節點 | 用途說明 |
| --- | --- |
| `/sys/power/state` | 控制系統 suspend 模式（可設定 `freeze`、`mem`、`disk`）。 |
| `/sys/power/wakeup_count` | 提供安全同步機制，避免 suspend 過程中新的喚醒事件遺漏。 |
| `/sys/kernel/debug/pm_debug` | 檢查 suspend / resume 流程的執行狀態與失敗原因。 |
| `/sys/kernel/debug/wakeup_sources` | 顯示所有喚醒來源與觸發統計。 |
| `dmesg | grep PM:` | 查看 suspend / resume 過程中的核心 log。 |
| `trace-cmd record -e power:*` | 追蹤整個電源事件時序。 |
| `powertop` | 分析系統耗電與喚醒頻率。 |

----------


## 9. 常見問題與排查

| 問題 | 可能原因 | 解決建議 |
| --- | --- | --- |
| 系統無法進入 suspend | 某些裝置仍在 busy 狀態或未進入 idle | 使用 `/sys/kernel/debug/pm_debug` 確認阻塞裝置。 |
| resume 後裝置無反應 | resume 階段未重新啟用 clock / regulator | 檢查驅動的 `.resume()` callback 是否正確實作。 |
| 無法被喚醒 | wakeup source 未啟用 | 確認 `device_init_wakeup(dev, true)` 是否有設定。 |
| suspend 過程發生 panic | 中斷未 disable 或 DMA 未停止 | 檢查 `.suspend()` 實作中 IRQ/DMA 的處理邏輯。 |
| suspend/resume 時序錯亂 | 裝置間相依關係未建立或缺少 power domain | 調整裝置依賴順序，或啟用 `genpd` trace 分析。 |
| 喚醒後功耗異常高 | runtime PM 未恢復正確 | 確認每個驅動的 runtime suspend/resume 是否被觸發。 |

**小提示：**

```bash
# 驗證裝置 suspend 狀態
cat /sys/kernel/debug/devices_deferred

# 查看所有可喚醒裝置
cat /sys/kernel/debug/wakeup_sources
```
----------

## 10. 驗證步驟與實務建議

1.  使用 `echo mem > /sys/power/state` 測試系統 suspend。
2.  觀察 dmesg，確認各 driver suspend/resume 是否成功。
3.  使用 `powertop` 檢查功耗是否下降。
4.  確認 wakeup 來源能正常喚醒（例如 GPIO、RTC）。
5.  若有 ATF，確認 PSCI call 成功執行。
6.  驅動層測試：
    -   加入 `pr_info()` 於 `.suspend()` / `.resume()` 驗證執行順序。
    -   驗證 clock/regulator 是否如預期關閉與開啟。
        

----------

**延伸閱讀**
-   `Documentation/power/suspend-and-hibernate.rst`
-   `Documentation/power/runtime_pm.rst`
-   `drivers/base/power/`
-   `drivers/firmware/psci/`
-   ARM Trusted Firmware: [https://trustedfirmware-a.readthedocs.io](https://trustedfirmware-a.readthedocs.io)
