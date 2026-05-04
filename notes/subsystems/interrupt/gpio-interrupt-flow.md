
# 🔗 GPIO → IRQ Domain → GIC → CPU 流程解析


# 🎯 本章目的

把以下全部串起來：

-   GPIO controller
-   interrupt-parent
-   irq_domain
-   Generic Interrupt Controller
-   Linux IRQ subsystem
-   user space（gpiomon）

👉 這一章是 **GPIO interrupt 真正的核心理解**

----------

# 🧭 一張圖先看懂
```
[GPIO pin edge]  
 ↓  
[GPIO controller]  
 ↓  
(child irq_domain)  
 ↓  
(parent irq_domain)  
 ↓  
[GIC]  
 ↓  
[CPU exception]  
 ↓  
[generic_handle_irq()]  
 ↓  
[driver ISR]  
 ↓  
[poll / epoll / gpiomon]
```
----------

# 1️⃣ Device Tree → Interrupt Flow 起點


## 裝置使用 GPIO interrupt
```
my_device {
    interrupt-parent = <&gpio3>;
    interrupts = <5 IRQ_TYPE_EDGE_FALLING>;
};
```
👉 代表：

-   使用 gpio3 的第 5 條 line
-   edge falling trigger

----------

## GPIO controller 定義
```
gpio3: gpio@xxxx {
    gpio-controller;
    interrupt-controller;
    #interrupt-cells = <2>;

    interrupt-parent = <&gic>;
    interrupts = <GIC_SPI 89 IRQ_TYPE_LEVEL_HIGH>;
};
```
👉 關鍵：

| 層級 | 說明 |  
|------|-------------------------|  
| device → gpio3 | 第一層 interrupt |  
| gpio3 → GIC | 第二層 interrupt |

----------

# 2️⃣ Kernel 初始化流程


## Step 1️⃣ GIC 初始化

driver：
```
drivers/irqchip/irq-gic-v3.c
```
建立：
```
gic_irq_domain
```
----------

## Step 2️⃣ GPIO controller probe

driver：
```
drivers/gpio/xxx.c
```
建立：
```
gpio_irq_domain
```
並設定：
```
irq_domain_create_hierarchy()
```
👉 建立：
```
gpio domain → gic domain
```
----------

# 3️⃣ IRQ mapping 建立

當 driver 或 gpiod request interrupt：
```
request_irq(...)
```
Kernel 會：
```
irq_create_mapping()
```
完成：
```
GPIO hwirq → GIC hwirq → Linux virq
```
----------

## 🔎 Mapping 範例
```
GPIO3_5  
 ↓  
hwirq = 5  
 ↓  
gpio domain translate  
 ↓  
GIC SPI = 89  
 ↓  
virq = 123
```
----------

# 4️⃣ Interrupt 發生時


## Step 1️⃣ 硬體觸發
```
GPIO pin 發生 edge
```
----------

## Step 2️⃣ GPIO controller

-   detect edge
-   產生 IRQ signal

----------

## Step 3️⃣ GIC 接收
```
SPI 89 active
```
GIC：

-   記錄 pending
-   根據 priority 選擇 CPU

----------

## Step 4️⃣ CPU exception

ARM CPU 進入：
```
IRQ exception handler
```
----------

## Step 5️⃣ Kernel IRQ handling
```
gic_handle_irq()  
  ↓  
generic_handle_irq(virq)
```
----------

## Step 6️⃣ driver ISR
```
my_irq_handler()
```
----------

## Step 7️⃣ user space（如果有）

例如：

-   gpiomon
-   poll()
-   epoll()

----------

# 5️⃣ gpiomon / libgpiod flow

## 設定 edge
```
gpiomon gpiochip0 5
```
內部：
```
GPIO_V2_LINE_FLAG_EDGE_*
```
----------

## interrupt → event flow
```
IRQ  
 ↓  
wake_up_interruptible()  
 ↓  
poll()  
 ↓  
read()  
 ↓  
user space event
```
----------

# 6️⃣ 重要：兩層 irq_domain


## child domain（GPIO）

負責：
```
GPIO pin → GPIO hwirq
```
----------

## parent domain（GIC）

負責：
```
GIC hwirq → CPU IRQ
```
----------

## 🔥 合起來
```
GPIO → gpio domain → GIC domain → CPU
```
----------

# 7️⃣ Debug


## Step 1️⃣ 看 interrupt 是否存在
```
cat /proc/interrupts
```
例如：
```
123:  10  0  GICv3  89  gpio-keys
```
----------

## Step 2️⃣ 觸發 GPIO

觀察：

-   counter 是否增加

----------

## Step 3️⃣ 如果沒增加

👉 問題在：

-   GPIO controller
-   irq_domain mapping
-   interrupt-parent
-   GIC

----------

## Step 4️⃣ 如果有增加但沒 event

👉 問題在：

-   driver ISR
-   poll / event

----------

# 8️⃣ 常見錯誤


## ❌ interrupt-parent 錯
```
device → gpio3 OK  
但 gpio3 沒接 GIC
```
→ 永遠不會進 CPU

----------

## ❌ trigger type 錯
```
LEVEL vs EDGE
```
→ interrupt 不觸發 / storm

----------

## ❌ irq_domain 沒建立

→ request_irq 失敗

----------

## ❌ pinctrl 沒設 input

→ edge 永遠不變


