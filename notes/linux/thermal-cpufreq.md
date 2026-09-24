# Linux Thermal / CPUFreq / OPP / DVFS 架構解析

本章介紹 Linux 電源與效能管理的核心機制：

- Thermal Framework（散熱管理）
- CPUFreq（CPU 調頻）
- OPP table（operating performance points）
- DVFS（Dynamic Voltage and Frequency Scaling）
- Cooling device（散熱裝置）
- 相關 Linux API 與 DTS 設定
- 實際平台（Renesas / Rockchip / i.MX）的行為差異
- Debug 與效能問題排查指南

## 1. Linux Thermal Framework 總覽

Linux thermal 偵測溫度並進行降頻、降電壓或降速。

架構：
```text
thermal sensor → thermal zone → governor → cooling device → hardware
```

| 元件 | 說明 |
| --- | --- |
| **thermal sensor** | SoC 溫度 ADC，如 tsadc（Rockchip）、thermal-zones |
| **thermal zone** | 一組邏輯溫度管理單位 |
| **governor** | 控制 cooling device 的演算法 |
| **cooling device** | CPUFreq / GPUFreq / 驅動控制器 |

## 2. Thermal Zone（主要控制邏輯）

DTS 常見設定：

```dts
thermal-zones {
    cpu_thermal: cpu-thermal {
        polling-delay-passive = <250>;
        polling-delay = <1000>;
        thermal-sensors = <&tsadc 0>;

        trips {
            trip_passive: trip-passive {
                temperature = <80000>;   // 80°C
                hysteresis = <2000>;     // 2°C
                type = "passive";
            };
            trip_critical: trip-critical {
                temperature = <110000>;
                hysteresis = <2000>;
                type = "critical";
            };
        };

        cooling-maps {
            map0 {
                trip = <&trip_passive>;
                cooling-device = <&cpu0 THERMAL_NO_LIMIT THERMAL_NO_LIMIT>;
            };
        };
    };
};
```

### 2.1 Trip points

| 類型 | 説明 |
|--------|----------------|
| passive | 降頻（CPUFreq/GPU） |
| active | 啟動風扇 |
| critical | 立即關機 |

## 3. Cooling Device（冷卻設備）

最常見：

-   **cpufreq cooling device**    
-   **gpufreq cooling device**
-   clock throttling 
-   thermal fan device（透過 PWM）

在 CPU cooling device 中，thermal 會要求：

```bash
set_cur_state(level)
```
kernel 依 level 降 CPU 頻率。

## 4. CPUFreq 架構

CPUFreq = CPU 調頻（Frequency scaling）

架構：

```text
policy → governor → frequency driver → hardware
```
元件 | 功能
------|------
policy | CPU cluster 的 frequency policy
governor | 調速演算法（performance, powersave, schedutil）
driver | 與 SoC clock / PMIC 互動
cpufreq table | 可用的頻率清單

## 5. CPUFreq Governor

常見 governor：

| governor | 行為 |
|----------|------|
| performance | 固定最高頻（bench 常用） |
| powersave | 固定最低頻 |
| ondemand | 舊式基於 CPU load 的 governor |
| schedutil（預設） | 與 CFS scheduler 整合，自動調頻 |

檢查 governor：

```bash
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```
切換 governor：

```bash
echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
```

## 6. OPP（Operating Performance Points）

OPP table 決定：

-   CPU 可用的頻率    
-   每個頻率所需電壓（VDD）
-   Turbo / Boost 模式  
-   是否需要穩壓器 (regulator)

OPP DTS 範例
```dts
opp-table {
    compatible = "operating-points-v2";
    opp-shared;

    opp-408000000 {
        opp-hz = /bits/ 64 <408000000>;
        opp-microvolt = <800000>;
    };
    opp-1200000000 {
        opp-hz = /bits/ 64 <1200000000>;
        opp-microvolt = <950000>;
        turbo-mode;
    };
};
```

kernel 依據 OPP 調整：

-   dvfs（動態電壓頻率）
-   thermal 降頻 
-   多 cluster 頻率同步（如 big.LITTLE）

## 7. DVFS（Dynamic Voltage & Frequency Scaling）

DVFS 是 OPP + clock + regulator 的整合。

流程：

```text
CPU load ↑
→ governor（schedutil）要求更高頻率
→ cpufreq driver 查 OPP
→ clock driver 調整 PLL
→ regulator driver 調整電壓
```
thermal 也會介入：

```text
temp ↑ → thermal trip
→ cooling device set_state
→ 降低 OPP（降低電壓 + 頻率）
```

## 8. 實際平台實例

### 8.1 Rockchip（RK3588）

-   tsadc + thermal-zones 
-   CPUFreq cooling device 整合完整
-   GPU cooling（Mali devfreq）也支援 DVFS
-   Android HWC + PowerHAL 也會調整頻率

### 8.2 Renesas（RZ/T2H）

-   CR52 firmware 使用 remoteproc 
-   A55 cluster thermal 走 SoC PMIC
-   OPP tables 在 SoC DTS 中固定
-   無 GPU → cooling device 主要是 CPUFreq

### 8.3 NXP i.MX8

-   NTC thermal sensors
-   cooling device 包含 ARM + VPU + GPU   
-   OPP 有 N系統 cluster 特殊依賴

## 9. User Space 觀察工具

檢查溫度
```bash
cat /sys/class/thermal/thermal_zone*/temp
```
檢查 CPU 頻率
```bash
cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq
```
檢查 OPP
```bash
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies
```
檢查 cooling device
```bash
ls /sys/class/thermal/cooling_device*
```

## 10. 常見問題與排查（Debug 技巧）

### 10.1 Thermal 不降頻？

可能原因：

-   cooling-map 未連結到 cpufreq cooling device    
-   DTS thermal-zone trip 配錯 
-   cpufreq driver 缺 OPP table
-   PMIC regulator 不支援 safe voltage

### 10.2 CPU 無法升頻？

原因：

-   governor 設 powersave 
-   OPP 標為 `opp-supported-hw` 導致被 mask  
-   thermal zone 降頻中

### 10.3 SoC 過熱重開機？

-   校正值錯誤（tsadc calibration）
-   DTS trip-critical 設太低
-   OPP 選太高電壓/頻率
