
# 🔁 IRQ Domain 與 Interrupt Mapping 深入解析


# 1️⃣ 為什麼需要 irq_domain？

在硬體世界：

-   每個 controller 都有自己的 **IRQ 編號（hwirq）**
-   GIC 有一套
-   GPIO controller 有一套
-   PCIe / MSI 也有自己的

但 Linux 需要的是：

> **統一的 IRQ number（virq）給 driver 使用**

👉 這個轉換機制就是 **irq_domain**

----------

# 🧠 核心概念
```
hwirq (hardware IRQ)  
 ↓  
irq_domain  
 ↓  
virq (Linux IRQ)
```
----------

# 2️⃣ 三種 IRQ number

| 名稱           | 說明                  | 例子         |
|----------------|-----------------------|--------------|
| hwirq          | 硬體 IRQ              | GPIO pin 5   |
| parent hwirq   | 上層 controller IRQ   | GIC SPI 45   |
| virq           | Linux IRQ             | 123          |

## 🔎 實際例子
```
GPIO3_5 → hwirq = 5
         ↓
gpio irq_domain translate
         ↓
GIC SPI = 45
         ↓
Linux virq = 123
```
----------

# 3️⃣ irq_domain 的作用

irq_domain 負責：

### ✅ 編號轉換

-   hwirq → virq

### ✅ hierarchical mapping

-   多層 controller 串接（GPIO → GIC）

### ✅ irq_chip 綁定

-   設定 handler / mask / ack

----------

# 4️⃣ IRQ Domain 類型


## 🔹 linear domain
```
irq_domain_add_linear()
```
用途：

-   hwirq 是連續數字
-   例如 GPIO controller

----------

## 🔹 tree domain
```
irq_domain_add_tree()
```
用途：

-   hwirq 不連續
-   需要動態 mapping

----------

## 🔹 hierarchical domain
```
irq_domain_create_hierarchy()
```
👉 用於：
```
GPIO → GIC  
PCIe → GIC  
MSI → GIC
```
----------

# 5️⃣ Hierarchical IRQ Flow
```
Device IRQ  
 ↓  
GPIO controller (child domain)  
 ↓  
irq_domain translate  
 ↓  
GIC (parent domain)  
 ↓  
CPU
```
👉 每一層都有自己的 irq_domain

----------

# 6️⃣ GPIO interrupt mapping

## Step 1️⃣ GPIO driver 建立 irq_domain
```
gpiochip_irqchip_add()
```
或：
```
irq_domain_add_linear()
```
----------

## Step 2️⃣ 建立 parent 關係
```
irq_set_parent()
```
或透過：
```
interrupt-parent (DT)
```
----------

## Step 3️⃣ mapping 發生

當 driver：
```
request_irq()
```
kernel 會：
```
irq_create_mapping()
```
完成：
```
hwirq → virq
```
----------

# 7️⃣ Device Tree 與 irq_domain


## GIC 定義
```
gic: interrupt-controller@xxxx {  
compatible = "arm,gic-v3";  
interrupt-controller;  
#interrupt-cells = <3>;  
};
```
----------

## GPIO controller
```
gpio3: gpio@xxxx {  
gpio-controller;  
interrupt-controller;  
#interrupt-cells = <2>;  
  
interrupt-parent = <&gic>;  
interrupts = <GIC_SPI 89 IRQ_TYPE_LEVEL_HIGH>;  
};
```
----------

## Device 使用
```
interrupt-parent = <&gpio3>;  
interrupts = <5 IRQ_TYPE_EDGE_FALLING>;
```
👉 這代表：

1.  GPIO domain 解析
2.  再 mapping 到 GIC

----------

# 8️⃣ Kernel 中的關鍵函式


## 建立 mapping
```
irq_create_mapping(domain, hwirq);
```
----------

## handler 呼叫
```
generic_handle_irq(virq);
```
----------

## domain translate
```
domain->ops->map()  
domain->ops->xlate()
```
----------

# 9️⃣ Debug irq_domain


## 1️⃣ 看 virq
```
cat /proc/interrupts
```
----------

## 2️⃣ 看 mapping
```
cat /sys/kernel/debug/irq/irqs/<irq>
```
----------

## 3️⃣ 看 domain
```
cat /sys/kernel/debug/irq_domain/*
```

----------

## 4️⃣ trace mapping
```
echo  function > /sys/kernel/debug/tracing/current_tracer  
echo irq_create_mapping > set_ftrace_filter
```
----------

# 🔎 常見錯誤

## ❌ IRQ 永遠不觸發

可能：

-   沒建立 irq_domain
-   interrupt-parent 錯
-   #interrupt-cells 錯

----------

## ❌ request_irq 失敗

可能：

-   mapping 不存在
-   IRQ number 無效

----------

## ❌ gpiomon 沒反應

原因：

👉 GPIO → GIC mapping 沒建立

----------

## ❌ IRQ number mismatch

看到：
```
GIC 45  
但 /proc/interrupts 是 123
```
👉 正常（virq ≠ hwirq）

----------

# 🔟 IRQ Flow
```
Hardware IRQ  
 ↓  
GIC hwirq  
 ↓  
irq_domain translate  
 ↓  
virq  
 ↓  
generic_handle_irq()  
 ↓  
driver ISR
```
----------

# 🧠 Stacked IRQ Domain

有些情況：
```
MSI → PCIe → GIC
```
或：
```
GPIO expander → I2C → GPIO → GIC
```
👉 會有多層 domain


