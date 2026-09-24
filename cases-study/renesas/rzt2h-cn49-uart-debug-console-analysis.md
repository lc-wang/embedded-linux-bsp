# RZ/T2H：使用 CN49（PMOD2）作為第二組 CPU UART Debug Console

## 1. 問題概述

在 RZ/T2H Evaluation Board 上，官方預設僅提供 **CN34（FT2232）** 作為主要 debug console（A55 / CR52 共用），以及 **CN79（USB gadget）** 作為 USB serial。

本次目標為：

-   在 **不使用 CN34、不使用 CN79** 的前提下
-   尋找並啟用一組 **從 CPU 直接拉出的 UART**
-   作為 **額外的 debug / log console**
-   不進行焊接修改

----------

## 2. 系統環境

### 2.1 可用硬體介面盤點結論

經比對 schematic 與板級配置後，確認：


| Connector        | 類型            | 是否 CPU 直出 | 備註                         |
|------------------|-----------------|---------------|------------------------------|
| CN34             | UART → FT2232   | 是            | 官方預設 Debug Console       |
| CN79             | USB Gadget      | 否            | 非 UART 腳位                 |
| CN49 (PMOD2)     | UART            | 是            | SCI1（P11_0 / P11_1）        |
| CN13             | UART            | 是            | 未焊接                       |


**唯一符合條件的介面為 CN49（PMOD2 UART）**

----------

### 2.2 CN49 UART 硬體對應關係

#### 2.2.1 SoC 腳位對應

CN49（PMOD2）對應 SoC 的 SCI1：

| SoC Pin | 功能      | CN49 |
|---------|-----------|------|
| P11_1   | SCI1_TXD  | TXD1 |
| P11_0   | SCI1_RXD  | RXD1 |


這兩個腳位為 **CPU 直出 UART（3.3V TTL）**，未經任何轉接 IC。

----------

### 2.3 UART 實體接線方式

#### 2.3.1 必要接線（最小）

| CN49（PMOD2） | USB-UART | 說明             |
|----------------|----------|------------------|
| TXD1           | RX       | 交叉接線         |
| RXD1           | TX       | 交叉接線         |
| GND            | GND      | 共地（必接）     |

GND

#### 2.3.2 注意事項

-   **一定要共地（GND ↔ GND）**
-   **USB-UART 的 VCC 不要接**
-   電平必須是 **3.3V TTL**
-   先不要接 RTS / CTS 

----------

## 3. 除錯過程

### 3.1 Linux DTS 設定

#### 3.1.1 啟用 SCI1 與 pinmux

```yaml
sci1_pins: sci1 {
        pinmux = <RZT2H_PORT_PINMUX(11, 1, 0x14)>, /* SCI1_TXD */
                 <RZT2H_PORT_PINMUX(11, 0, 0x14)>; /* SCI1_RXD */
};

&sci1 {
        pinctrl-0 = <&sci1_pins>;
        pinctrl-names = "default";
        status = "okay";
};
```

#### 3.1.2 確認 Linux 端裝置節點

`ls /dev/ttySC* # /dev/ttySC0 /dev/ttySC1 /dev/ttySC3` 

經測試，CN49 對應 **`/dev/ttySC1`**。

----------

### 3.2 Runtime Pinmux 驗證

使用 debugfs 確認 pinmux 實際生效狀態：
```yaml
mount -t debugfs none /sys/kernel/debug

grep -R "P11_0\|P11_1" /sys/kernel/debug/pinctrl/*/pinmux-pins 
```
輸出：
```yaml
pin 88 (P11_0): device 80005400.serial  function sci1 group sci1
pin 89 (P11_1): device 80005400.serial  function sci1 group sci1 
```
確認 **P11_0 / P11_1 已正確切換為 SCI1**。

----------

### 3.3 實體線路問題：DIP Switch 關鍵影響

#### 3.3.1 問題現象

-   板子 → PC（TX）正常   
-   PC → 板子（RX）完全無反應
-   TX/RX loopback 測試失敗
-   出現亂碼、單向通訊等異常

----------

## 4. Root Cause 分析

在 EVB schematic 中可確認：
```yaml
P11_0_BSC_A5_LCDC_DATG0_PMOD2_RXD1
             │
        DIP-Switch-10pol-SMD (SW6)
```
**CN49 的 UART 腳位實際上是經過 10-pin DIP switch 才會接通**

----------

## 5. 解決方案

### 5.1 必要設定

-   **SW6-4 = ON**  
    → 連接 `P11_0 → PMOD2_RXD1`
    

同時需確保：

-   其他與 P11_0 共用的功能（ESC_RESETOUT 等）**未同時打開**

----------

### 5.2 通訊測試指令

#### 5.2.1 設定 UART 參數

```bash
stty -F /dev/ttySC1 115200 cs8 -cstopb -parenb \
     -ixon -ixoff -crtscts -echo -icrnl -inlcr -opost
```

#### 5.2.2 板子 → PC

`echo  "hello CN49" > /dev/ttySC1` 

#### 5.2.3 PC → 板子

`cat -v /dev/ttySC1` 

成功後可雙向收發，無亂碼。
