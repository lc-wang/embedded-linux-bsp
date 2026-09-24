# Rockchip RK3588 + RK806 Power-Off Call Flow & Kernel Debugging Analysis

## 1. 問題概述（觀察現象）

在 RK3588 + RK806 平台上，執行：

`poweroff` 

實際行為卻為 **系統重新啟動（reboot）**，而非硬體斷電。

透過以下方式確認 kernel 內存在 power-off 相關符號：

`cat /proc/kallsyms | grep pm_power_off` 

可觀察到：
```bash
legacy_pm_power_off
rk_pm_power_off_delay_work
rk808_pm_power_off_dummy 
```
然而，實際執行流程中 **並未進入 rk808_pm_power_off_dummy**，因此 RK808 dummy poweroff **不構成此次問題核心**。

## 2. 分析過程

### 2.1 Linux Power-Off 的實際 Call Flow（RK3588）

#### 2.1.1 Userspace → Kernel
```yaml
poweroff
 └─ systemd → reboot(LINUX_REBOOT_CMD_POWER_OFF)
```
#### 2.1.2 Kernel 核心流程
```yaml
sys_reboot()
 └─ kernel_power_off()
     ├─ device_shutdown()
     │   └─ 各 driver shutdown callback
     └─ if (pm_power_off)
         └─ pm_power_off()
     └─ fallback → machine_restart() 
```
**關鍵重點：**

-   `pm_power_off` 若為 `NULL` 或「無效實作」
    
-   kernel **不會 busy wait**
    
-   最終行為會 **退回 restart path**
    
### 2.2 RK806 在 Driver 架構中的定位

#### 2.2.1 RK806 Driver 類型

RK806 屬於新世代 Rockchip PMIC family，與 RK808 / RK809 在設計與角色上有本質差異：

| PMIC  | Bus | Driver 類型                    | Poweroff 角色                         |
|-------|-----|--------------------------------|----------------------------------------|
| RK808 | I2C | Legacy MFD                     | 可選 system-power-controller           |
| RK809 | I2C | Legacy MFD                     | 可選 system-power-controller           |
| RK806 | SPI | New-gen MFD + Regulator driver | 被動電源元件（不主導系統 Poweroff）   |

#### 2.2.2 RK806 不註冊 pm_power_off

在 `drivers/mfd/rk806-core.c`：
```c
static  const  struct  of_device_id  rk806_of_match[] = {
    { .compatible = "rockchip,rk806" },
};
```
**重要事實：**

-   RK806 driver **沒有設定 `pm_power_off`**
    
-   RK806 **不是 system power controller**
    
-   RK806 不會主動決定 poweroff 行為
    
### 2.3 RK806 的真實 Power-Off Call Flow

#### 2.3.1 DTS 定義（關鍵）
```dts
pinctrl-names = "default", "pmic-power-off";
pinctrl-1 = <&rk806_dvs1_pwrdn>;
```
#### 2.3.2 Driver 行為（regulator shutdown path）

在 `drivers/regulator/rk806-regulator.c`：
```c
if ((rk806->pins->p) && (rk806->pins->power_off))
    pinctrl_select_state(rk806->pins->p,
                          rk806->pins->power_off);
```
#### 2.3.3 實際 Call Flow
```yaml
kernel_power_off()
 └─ device_shutdown()
     └─ regulator shutdown
         └─ rk806_regulator_shutdown()
             └─ pinctrl_select_state("pmic-power-off")
                 └─ 拉動 PWRDN 腳位
```
注意：**到這裡為止，kernel 的責任結束**

### 2.4 Kernel 與 Hardware 的責任邊界

#### Kernel 保證的事情

-   呼叫 `pinctrl_select_state()`
    
-   將 PMIC 控制腳位切換到 power-off 狀態
    
#### Kernel 不知道的事情

-   PWRDN 是 active-high 還是 active-low
    
-   後面是否接 load switch / MCU
    
-   是否仍有 always-on rail
    
-   是否 SoC 本身仍有 reset source
    
**是否真的斷電 = 硬體設計責任**

### 2.5 ftrace：用來確認「有沒有走到那裡」

#### 2.5.1 啟用 function trace
```bash
echo  function > /sys/kernel/tracing/current_tracer echo rk806 > /sys/kernel/tracing/set_ftrace_filter
```
#### 2.5.2 為什麼 poweroff 後是空的？

-   poweroff 是 **terminal event**
    
-   CPU 直接 reset / power loss
    
-   trace buffer 尚未 flush
    
**這不是 ftrace 無效，而是 poweroff 特性**

### 2.6 printk vs pr_emerg vs pstore

#### 2.6.1 printk / pr_info 的限制

-   依賴 console / log buffer
    
-   poweroff 時 **極可能來不及輸出**
    
#### 2.6.2 pr_emerg 的意義

`pr_emerg("rk806: entering power-off\n");` 

-   最高 log level
    
-   優先嘗試同步輸出
    
-   但 **仍不保證保存**
    
#### 2.6.3 pstore

##### 啟用條件

-   `CONFIG_PSTORE`
    
-   `CONFIG_PSTORE_RAM` 或 EFI backend
    
##### 使用方式

`ls /sys/fs/pstore` 

##### 適用場景

-   panic
    
-   reboot
    
-   poweroff 前最後訊息
    
**pstore 是唯一能跨 reboot 保存證據的工具**

### 2.7 dynamic_debug 為什麼幫助有限

-   dynamic_debug 依賴 **正常執行期間**
    
-   poweroff path 時間極短
    
-   更適合用在：
    
    -   probe
        
    -   suspend/resume
        
    -   regulator enable/disable
        
### 2.8 kallsyms / vmlinux / faddr2line

#### 2.8.1 kallsyms

確認 symbol 是否存在、是否被編譯進 kernel：
```bash
cat /proc/kallsyms | grep rk806
```
#### 2.8.2 vmlinux + faddr2line
```bash
aarch64-linux-gnu-addr2line -e vmlinux ffffffc0xxxxxxxx
```
用途：

-   address → function → source file
    
-   驗證實際執行位置
    
## 3. Root Cause 分析（為什麼會「Poweroff → Reboot」）

### 3.1 合法推論鏈

1.  RK806 被動接受 PWRDN
    
2.  硬體未完全切斷 SoC 電源
    
3.  SoC 偵測到 reset condition
    
4.  BootROM 重新啟動
    
5.  表現為「reboot」
    
**Kernel 已完成它該做的事**

## 4. 結論與建議

### 4.1 結論

-   RK806 **不是 system power controller**
    
-   Kernel poweroff 流程 **已正確執行**
    
-   `pinctrl_select_state("pmic-power-off")` 為最後責任點
    
-   實際是否斷電，取決於 **硬體電源樹設計**
    
-   poweroff → reboot **不是 kernel bug**

### 4.2 RK808 vs RK806：世代差異總結

#### 4.2.1 RK806 Driver 類型

RK806 屬於新世代 Rockchip PMIC family，與 RK808 / RK809 在設計與角色上有本質差異：

| PMIC  | Bus | Driver 類型                    | Poweroff 角色                         |
|-------|-----|--------------------------------|----------------------------------------|
| RK808 | I2C | Legacy MFD                     | 可選 system-power-controller           |
| RK809 | I2C | Legacy MFD                     | 可選 system-power-controller           |
| RK806 | SPI | New-gen MFD + Regulator driver | 被動電源元件（不主導系統 Poweroff）   |
