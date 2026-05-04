
# Interrupt Debug Playbook

這一章是把以下內容落地：

-   GPIO interrupt
-   irq_domain
-   Generic Interrupt Controller
-   driver ISR
-   user space（gpiomon / poll）

👉 變成一套「**可以直接解問題的流程**」

----------

# 🎯 Interrupt 問題本質分類

所有 interrupt 問題，幾乎都落在這 6 類：

1. GPIO 沒產生 interrupt  
2. GPIO → GIC 沒接上（irq_domain / DT）  
3. GIC 沒送到 CPU  
4. CPU 收到但 driver 沒處理  
5. driver 處理但 user space 沒看到  
6. trigger type / polarity 錯誤（最常見）

----------

# 🧭 標準 Debug 流程
```
Step 1  → 確認 DT interrupt 設定  
Step 2  → 確認 pinctrl（input + mux）  
Step 3  → 確認 irq_domain mapping  
Step 4  → 確認 GIC 有收到  
Step 5  → 確認 CPU IRQ counter 增加  
Step 6  → 確認 driver ISR 有跑  
Step 7  → 確認 user space event
```
----------

# 🔎 Case 1：gpiomon 完全沒反應


## Step 1️⃣ 檢查 Device Tree
```
interrupt-parent = <&gpio3>;  
interrupts = <5 IRQ_TYPE_EDGE_FALLING>;
```
確認：

-   GPIO line 正確
-   trigger type 正確

----------

## Step 2️⃣ 檢查 pinctrl
```
cat /sys/kernel/debug/pinctrl/*/pinmux-pins
```
確認：

-   該 pin 是 `gpio`
-   設為 `input`

👉 沒設 input = 永遠不會有 edge

----------

## Step 3️⃣ 檢查 GPIO controller 是否支援 IRQ
```
gpio3: gpio@xxxx {
    interrupt-controller;
};
```
----------

## Step 4️⃣ 檢查 mapping
```
cat /proc/interrupts
```
如果完全沒有該 IRQ：

👉 問題在：

-   interrupt-parent
-   irq_domain
-   GPIO driver

----------

## Step 5️⃣ 手動觸發

用：
```
gpioset / gpioget
```
或直接硬體觸發

----------

# 🔎 Case 2：/proc/interrupts 沒有 entry

👉 代表 interrupt **根本沒註冊成功**

----------

## 可能原因

### ❌ interrupt-parent 錯
```
interrupt-parent = <&wrong_node>;
```
----------

### ❌ gpio 沒接 GIC

缺少：
```
interrupt-parent = <&gic>;
```
（或 inheritance 錯）

----------

### ❌ #interrupt-cells 錯
```
#interrupt-cells = <2>;
```
與 driver 不匹配

----------

### ❌ driver 沒 request_irq
```
request_irq(...)
```
沒被呼叫

----------

# 🔎 Case 3：counter 不增加
```
cat /proc/interrupts
```
看到：
```
123: 0 0 GICv3 89 gpio-keys
```
👉 一直是 0

----------

## 可能原因

### ❌ GPIO 沒輸出 interrupt

-   pinctrl 沒設 input
-   edge 沒變化

----------

### ❌ trigger type 錯
```
EDGE vs LEVEL  
HIGH vs LOW
```
👉 最常見

----------

### ❌ polarity 錯

active-low 搞反

----------

# 🔎 Case 4：interrupt storm
```
CPU usage 100%  
interrupt 不斷觸發
```
----------

## 常見原因

### ❌ level-trigger 沒 clear

device interrupt flag 沒清

----------

### ❌ edge 設成 level

應該 EDGE_FALLING  
卻設 LEVEL_LOW

----------

### ❌ open drain + pull-up 問題

line 永遠被拉低

----------

# 🔎 Case 5：ISR 沒被呼叫


## 檢查 request_irq
```
request_irq(virq, handler, ...);
```
----------

## 用 ftrace
```
echo  function > /sys/kernel/debug/tracing/current_tracer  
echo irq_handler_entry > set_ftrace_filter
```
----------

## 看 log
```
dmesg | grep irq
```
----------

# 🔎 Case 6：user space 沒事件


## 可能原因

-   driver 沒 propagate event
-   poll 沒 wake
-   沒設定 edge flag

----------

## 檢查 libgpiod
```
gpiomon gpiochip0 5
```
----------

# 🔎 Case 7：GPIO 有動但沒 IRQ

👉 非常常見


## 原因

GPIO output OK  
但 interrupt path 壞掉

----------

## 檢查

-   irq_domain
-   interrupt-parent
-   GIC mapping

----------

# 🧠 進階 Debug

## 查看 IRQ 詳細資訊
```
cat /sys/kernel/debug/irq/irqs/<n>
```
----------

## 查看 irq_domain
```
cat /sys/kernel/debug/irq_domain/*
```
----------

## Trace IRQ flow
```
echo  function > tracing/current_tracer  
echo gic_handle_irq > set_ftrace_filter
```
----------

## dynamic debug
```
echo  'file drivers/irqchip/* +p' > dynamic_debug/control
```
----------

# 🔬 硬體驗證

👉 不要只看軟體

用：

-   示波器
-   logic analyzer

確認：

GPIO 是否真的有 edge

----------

# 🧭 最終 Debug Flow
```
GPIO edge 有沒有？  
 ↓  
/proc/interrupts 有沒有？  
 ↓  
counter 有沒有增加？  
 ↓  
ISR 有沒有跑？  
 ↓  
user space 有沒有收到？
```

