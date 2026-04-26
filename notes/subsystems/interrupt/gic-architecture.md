
# 🧠 GIC（Generic Interrupt Controller）架構解析

# 1️⃣ GIC 在系統中的角色

Generic Interrupt Controller

在 ARM SoC 中，GIC 是：

> **所有 interrupt 的最終仲裁者與分發中心**

不論來源是：

-   GPIO
-   UART
-   I2C
-   PCIe
-   Display (vblank)
-   Timer

👉 **最終都會進入 GIC，再送到 CPU**

----------

# 🧭 整體架構位置
```
Peripheral (GPIO / UART / etc)  
 ↓  
Interrupt Source  
 ↓  
Controller (GPIO controller / IP block)  
 ↓  
irq_domain (Linux mapping)  
 ↓  
GIC (硬體 interrupt controller)  
 ↓  
CPU core  
 ↓  
Linux IRQ subsystem  
 ↓  
Driver ISR
```
👉 GIC 是「硬體層最後一站」。

----------

# 2️⃣ GIC 的核心功能

GIC 負責：

### ✅ Interrupt Routing

-   決定 interrupt 要送到哪個 CPU core

### ✅ Priority 管理

-   interrupt priority（高優先先處理）

### ✅ Mask / Enable

-   控制 interrupt 是否允許進 CPU

### ✅ Trigger type

-   edge-triggered / level-triggered

----------

# 3️⃣ GIC 架構（GICv2 / GICv3）


## 🔹 GICv2（較舊）
```
 ┌────────────┐  
 │  CPU IF    │  
 └────┬───────┘  
      │  
 ┌───────▼────────┐  
 │ Distributor    │  
 └───────┬────────┘  
         │  
 Interrupt sources
```
----------

## 🔹 GICv3（現代 SoC）
```
 ┌────────────┐  
 │  CPU IF    │  
 └────┬───────┘  
      │  
 ┌───────▼────────┐  
 │ Redistributor  │ (per CPU)  
 └───────┬────────┘  
         │  
 ┌───────▼────────┐  
 │ Distributor    │  
 └───────┬────────┘  
         │  
 Interrupt sources
```
----------

## 📌 關鍵差異

| 元件 | GICv2 | GICv3 |  
|-----------------|-------|-------|  
| CPU interface | 共用 | 每 CPU |  
| Redistributor | ❌ | ✅ |  
| scalability | 低 | 高 |  
| multicore | 基本 | 強 |

----------

# 4️⃣ Interrupt 類型（SPI / PPI / SGI）


## 🔹 SPI（Shared Peripheral Interrupt）

SPI = 外部裝置 interrupt

例如：

-   GPIO interrupt
-   UART
-   PCIe

👉 多 CPU 共享

----------

## 🔹 PPI（Private Peripheral Interrupt）

PPI = CPU 私有 interrupt

例如：

-   local timer

👉 每個 CPU 獨立

----------

## 🔹 SGI（Software Generated Interrupt）

SGI = software trigger interrupt

用途：

-   CPU 間 communication（IPI）

----------

## 📌 Summary

| 類型 | 用途 |  
|------|--------------|  
| SPI | 外部 device |  
| PPI | CPU 私有 |  
| SGI | CPU 間 |

----------

# 5️⃣ GIC 中的 IRQ 編號

GIC 內部有：
```
hwirq (hardware IRQ)
```
例如：
```
SPI 45
```
但 Linux 看到的是：
```
IRQ 123
```
👉 透過 irq_domain mapping 轉換

----------

# 6️⃣ GIC Device Tree 描述

典型 GICv3：
```
gic: interrupt-controller@f9000000 {  
 compatible = "arm,gic-v3";  
 interrupt-controller;  
 #interrupt-cells = <3>;  
 reg = <...>;  
};
```
----------

## 🔎 #interrupt-cells = <3> 是什麼？

格式：
```
<type number flags>
```
例如：
```
interrupts = <GIC_SPI 45 IRQ_TYPE_LEVEL_HIGH>;
```
代表：
| 欄位 | 意義 |  
|------------|---------------------|  
| GIC_SPI | SPI 類型 |  
| 45 | interrupt number |  
| LEVEL_HIGH | trigger type |

----------

# 7️⃣ interrupt-parent 關係

裝置：
```
uart0: serial@xxxx {  
 interrupt-parent = <&gic>;  
 interrupts = <GIC_SPI 45 IRQ_TYPE_LEVEL_HIGH>;  
};
```
👉 表示：

-   interrupt 最終交給 GIC 處理

----------

# 8️⃣ Linux Kernel 中的 GIC driver

核心檔案：
```
drivers/irqchip/irq-gic-v3.c
```
負責：

-   初始化 GIC
-   建立 irq_domain
-   註冊 interrupt handler
-   與 CPU interface 溝通

----------

# 9️⃣ Interrupt 處理流程（Kernel）
```
Hardware IRQ  
 ↓  
GIC 接收  
 ↓  
CPU interrupt exception  
 ↓  
gic_handle_irq()  
 ↓  
generic_handle_irq()  
 ↓  
driver ISR
```
👉 所有 interrupt 都會經過這條路。

----------

# 🔟 為什麼 一定要懂 GIC？

因為以下全部會壞：

-   GPIO interrupt
-   touch
-   camera
-   DRM vblank
-   PCIe interrupt
-   WiFi interrupt

👉 根本原因通常在：

GIC / IRQ mapping / trigger type

----------

# 🔎 常見錯誤


## ❌ interrupt 沒觸發

可能：

-   trigger type 錯
-   GIC 沒 enable
-   routing 錯

----------

## ❌ interrupt storm

原因：

-   level trigger 沒 clear
-   polarity 錯

----------

## ❌ 多核心問題

原因：

-   IRQ affinity 沒設
-   分到錯誤 CPU
