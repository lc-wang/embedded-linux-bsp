# GPIO → IRQ Domain → GIC → CPU 流程解析

把以下全部串起來：

-   GPIO controller
-   interrupt-parent
-   irq_domain
-   Generic Interrupt Controller
-   Linux IRQ subsystem
-   user space（gpiomon）

這一章是 **GPIO interrupt 真正的核心理解**

## 1. 一張圖先看懂

```text
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

## 2. Device Tree → Interrupt Flow 起點

### 2.1 裝置使用 GPIO interrupt

```dts
my_device {
    interrupt-parent = <&gpio3>;
    interrupts = <5 IRQ_TYPE_EDGE_FALLING>;
};
```
代表：

-   使用 gpio3 的第 5 條 line
-   edge falling trigger

### 2.2 GPIO controller 定義

```dts
gpio3: gpio@xxxx {
    gpio-controller;
    interrupt-controller;
    #interrupt-cells = <2>;

    interrupt-parent = <&gic>;
    interrupts = <GIC_SPI 89 IRQ_TYPE_LEVEL_HIGH>;
};
```
關鍵：

| 層級 | 說明 |  
|------|-------------------------|  
| device → gpio3 | 第一層 interrupt |  
| gpio3 → GIC | 第二層 interrupt |

## 3. Kernel 初始化流程

### 3.1 Step 1 GIC 初始化

driver：
```text
drivers/irqchip/irq-gic-v3.c
```
建立：
```text
gic_irq_domain
```

### 3.2 Step 2 GPIO controller probe

driver：
```text
drivers/gpio/xxx.c
```
建立：
```text
gpio_irq_domain
```
並設定：
```text
irq_domain_create_hierarchy()
```
建立：
```text
gpio domain → gic domain
```

## 4. IRQ mapping 建立

當 driver 或 gpiod request interrupt：
```text
request_irq(...)
```
Kernel 會：
```text
irq_create_mapping()
```
完成：
```text
GPIO hwirq → GIC hwirq → Linux virq
```

### 4.1 Mapping 範例

```text
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

## 5. Interrupt 發生時

### 5.1 Step 1 硬體觸發

```text
GPIO pin 發生 edge
```

### 5.2 Step 2 GPIO controller

-   detect edge
-   產生 IRQ signal

### 5.3 Step 3 GIC 接收

```text
SPI 89 active
```
GIC：

-   記錄 pending
-   根據 priority 選擇 CPU

### 5.4 Step 4 CPU exception

ARM CPU 進入：
```text
IRQ exception handler
```

### 5.5 Step 5 Kernel IRQ handling

```text
gic_handle_irq()  
  ↓  
generic_handle_irq(virq)
```

### 5.6 Step 6 driver ISR

```text
my_irq_handler()
```

### 5.7 Step 7 user space（如果有）

例如：

-   gpiomon
-   poll()
-   epoll()

## 6. gpiomon / libgpiod flow

### 6.1 設定 edge

```bash
gpiomon gpiochip0 5
```
內部：
```text
GPIO_V2_LINE_FLAG_EDGE_*
```

### 6.2 interrupt → event flow

```text
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

## 7. 重要：兩層 irq_domain

### 7.1 child domain（GPIO）

負責：
```text
GPIO pin → GPIO hwirq
```

### 7.2 parent domain（GIC）

負責：
```text
GIC hwirq → CPU IRQ
```

### 7.3 合起來

```text
GPIO → gpio domain → GIC domain → CPU
```

## 8. 常見問題與排查

### 8.1 Debug

#### Step 1 看 interrupt 是否存在

```bash
cat /proc/interrupts
```
例如：
```text
123:  10  0  GICv3  89  gpio-keys
```

#### Step 2 觸發 GPIO

觀察：

-   counter 是否增加

#### Step 3 如果沒增加

問題在：

-   GPIO controller
-   irq_domain mapping
-   interrupt-parent
-   GIC

#### Step 4 如果有增加但沒 event

問題在：

-   driver ISR
-   poll / event

### 8.2 常見錯誤

#### interrupt-parent 錯

```text
device → gpio3 OK  
但 gpio3 沒接 GIC
```
→ 永遠不會進 CPU

#### trigger type 錯

```text
LEVEL vs EDGE
```
→ interrupt 不觸發 / storm

#### irq_domain 沒建立

→ request_irq 失敗

#### pinctrl 沒設 input

→ edge 永遠不變
