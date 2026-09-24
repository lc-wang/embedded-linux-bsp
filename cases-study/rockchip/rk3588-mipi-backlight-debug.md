# RK3588 MIPI Panel 背光 PWM 只能 ON/OFF 之除錯報告

## 1. 問題概述（Symptom）

在其中一片 RK3588 板子上出現以下狀況：

-   `/sys/class/backlight/backlight/brightness` 可正常寫入
    
-   `/sys/class/backlight/backlight/actual_brightness` 會跟著變
    
-   `/sys/kernel/debug/pwm` 顯示：
    
    -   PWM 已 enable
        
    -   duty / period 會隨 brightness 改變
        
-   **實際面板亮度只有兩種狀態：**
    
    -   `brightness = 0` → 螢幕全黑
        
    -   `brightness > 0` → 螢幕全亮
        
-   **亮度無法隨數值平滑變化（只有 ON / OFF）**
    

----------

## 2. 系統環境

-   平台：**Rockchip RK3588**
    
-   顯示介面：**MIPI DSI**
    
-   Panel：**TQ101AJ02**
    
-   背光 IC：**TPS61165**
    
-   背光控制方式：
    
    -   `BL_EN`：背光電源開關
        
    -   `PWM (CTRL)`：亮度調光
        
-   Linux driver：
    
    -   DRM
        
    -   `pwm-backlight`
        

----------

## 3. 除錯過程

### 3.1 初步假設與排除方向

曾懷疑但最終排除的項目：

-   ✗ `pwm-backlight` driver 問題
    
-   ✗ PWM 頻率設定錯誤
    
-   ✗ DTS / pinmux 設定錯誤
    
-   ✗ panel driver（simple-panel / vendor panel）不相容
    

最終證實為：

-   ✓ **單一板子的板級硬體問題（Bad board）**
    

----------

### 3.2 實際除錯流程

#### 3.2.1 Backlight sysfs 確認
```
ls /sys/class/backlight/
ls /sys/class/backlight/backlight/

cat /sys/class/backlight/backlight/type
cat /sys/class/backlight/backlight/max_brightness
cat /sys/class/backlight/backlight/brightness
cat /sys/class/backlight/backlight/actual_brightness
```
直接測試亮度寫入：

```
echo 1   > /sys/class/backlight/backlight/brightness
echo 50  > /sys/class/backlight/backlight/brightness
echo 255 > /sys/class/backlight/backlight/brightness

cat /sys/class/backlight/backlight/brightness
cat /sys/class/backlight/backlight/actual_brightness
```

結果：  
→ sysfs 行為正常，數值可寫、可讀。

----------

#### 3.2.2 PWM framework 狀態確認（debugfs）

確保 debugfs 已掛載：

`mount | grep debugfs || mount -t debugfs none /sys/kernel/debug` 

查看所有 PWM：

`cat /sys/kernel/debug/pwm` 

只抓背光相關行：
```
cat /sys/kernel/debug/pwm | sed -n '/backlight *):/,+0p'
cat /sys/kernel/debug/pwm | sed -n '/backlight-lvds *):/,+0p'
```
驗證 brightness 寫入會驅動 PWM duty 改變：
```
echo 1   > /sys/class/backlight/backlight/brightness
cat /sys/kernel/debug/pwm | sed -n '/backlight *):/,+0p'

echo 50  > /sys/class/backlight/backlight/brightness
cat /sys/kernel/debug/pwm | sed -n '/backlight *):/,+0p'

echo 255 > /sys/class/backlight/backlight/brightness
cat /sys/kernel/debug/pwm | sed -n '/backlight *):/,+0p'
```

結果：  
→ **PWM period / duty 確實隨亮度改變**  
→ 排除 PWM framework / pwm-backlight driver 未動作的可能性。

----------

#### 3.2.3 PWM 是否已被 driver 佔用（export 測試）
```
ls /sys/class/pwm/
ls /sys/class/pwm/pwmchip0/
ls /sys/class/pwm/pwmchip1/

echo 0 > /sys/class/pwm/pwmchip0/export
```
若出現：

`Device or resource busy` 

→ 表示 PWM 已被 backlight driver request（符合預期）。

----------

#### 3.2.4 Device Tree 路徑確認

確認 DT 內有 backlight 與 panel node：
```
grep -R backlight /proc/device-tree -n
grep -R panel /proc/device-tree -n
```
----------

#### 3.2.5 PWM pinmux 驗證

`grep -R "pwm13" -n /sys/kernel/debug/pinctrl/* 2>/dev/null | head -n 120` 

最關鍵輸出：

`grep -R "pwm13" -n /sys/kernel/debug/pinctrl/*/pinmux-pins 2>/dev/null | head -n 120` 

實際看到：

`pin 110 (gpio3-14): febf0010.pwm function pwm13 group pwm13m0-pins` 

結論：

-   PWM 腳位已正確 mux 成 PWM function
    
-   排除「PWM 只在 controller 內變化，實體腳位仍是 GPIO」的問題
    

----------

#### 3.2.6 低頻 PWM 驗證（100Hz Flicker Test）

為了肉眼觀察 PWM 是否真正影響背光，暫時將 PWM 設為低頻：

`pwms = <&pwm13 0 10000000 0>;  // 10ms = 100Hz` 

開機後再次確認：

`cat /sys/kernel/debug/pwm | sed -n '/backlight *):/,+0p'` 

觀察結果：

-   即使 duty 改變
    
-   實際亮度仍只有 ON / OFF
    

→ 高度懷疑 **板級硬體或訊號路徑問題**

----------

### 3.3 關鍵實驗：更換板子

在以下條件完全相同的情況下：

-   相同 Linux image
    
-   相同 DTS
    
-   相同 panel
    

僅更換 **另一片RK3588 板子**。

結果：

-   原板 ✗：亮度只能 ON / OFF
    
-   新板 ✓：亮度可正常平滑調整
    

----------

## 4. Root Cause 分析

> **此問題並非 Linux driver、DTS、PWM 設定或 pinmux 問題。**
> 
> 根本原因為：  
> **單一板子的板級硬體異常（Bad board）**

可能原因包含：

-   PWM 訊號線開路 / 短路
    
-   背光 CTRL 腳被強拉高
    
-   電阻錯料 / 未焊
    
-   焊接或組裝不良
    
-   BOM 或 routing 差異
    

----------

## 5. 結論與建議（經驗與教訓）

### 5.1 `debug/pwm` 正常 ≠ 實體腳位真的在 PWM

PWM controller 狀態正確，不代表訊號一定到達背光 IC。

### 5.2 pinmux 驗證是必做步驟

`/sys/kernel/debug/pinctrl/*/pinmux-pins` 

### 5.3 100Hz PWM 是最快的硬體 sanity check

比反覆修改 driver / DTS 更有效率。

### 5.4 當出現以下組合時，應優先懷疑硬體

-   PWM duty 有變
    
-   pinmux 正確
    
-   schematic 正確
    
-   實際亮度只有 ON / OFF
    

→ **直接換板或量測背光 CTRL 腳**

----------

## 附錄

### A. 快速 Debug Checklist（TL;DR）
```
brightness sysfs OK？
→ cat /sys/kernel/debug/pwm | sed -n '/backlight *):/,+0p'
→ pinmux-pins 確認 PWM function
→ 100Hz flicker test
→ disable enable-gpio 測試
→ 換板驗證
```
