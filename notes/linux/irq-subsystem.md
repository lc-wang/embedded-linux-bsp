# Linux IRQ Subsystem 解析：GIC / IRQ Domain / SoftIRQ / Threaded IRQ

本章介紹 Linux 中斷架構，包括：

- IRQ 號碼與 IRQ domain
- 中斷控制器（GIC）
- interrupt flow
- request_irq / free_irq
- threaded IRQ
- softirq / tasklet
- interrupt affinity（綁定 CPU）
- interrupt latency 與常見 debug 方法

## 1. IRQ Subsystem 總覽

Linux 將中斷分為三階段：
```yaml
硬體中斷 (HW Interrupt)
↓
中斷控制器 (GIC)
↓
Linux IRQ handler (top half)
↓
SoftIRQ / Tasklet / Workqueue (bottom half)
```

目的：

- 硬中斷處理越快越好  
- 耗時工作下推至 bottom-half  

## 2. IRQ Number、IRQ Domain、hwirq

設備樹中描述：
```dts
interrupts = <42 IRQ_TYPE_LEVEL_HIGH>;
```

映射流程：
```yaml
HW IRQ number (hwirq)
↓
IRQ Domain
↓
Linux Virtual IRQ number (virq)
```
主要結構：
```c
struct irq_domain
struct irq_data
struct irq_desc
```

不同 SoC 使用不同 IRQ controller：

- ARM GIC v2 / v3  
- RZ/V2H → GIC-600  
- i.MX8 → GIC + local interrupt  

## 3. GIC（Generic Interrupt Controller）

ARM 常見中斷控制器：

| 版本 | 特點 |
|------|------|
| GICv2 | 傳統分層，常用於 Cortex-A7/A9 |
| GICv3 | 支援多核、可擴展 routing model |
| GICv4 | 支援 virtualization，給 hypervisor 用 |

GIC 的功能：

- interrupt routing 至不同 CPU
- 設定觸發方式（edge/level）
- 設定 interrupt priority
- mask/unmask
- 中斷確認 & 結束（EOI）

## 4. request_irq 與中斷 Handler

一般驅動：

```c
int request_irq(unsigned int irq,
        irq_handler_t handler,
        unsigned long flags,
        const char *name,
        void *dev);
 ```

### 4.1 常見 flags

| flag | 用途 |
|------|------|
| IRQF_TRIGGER_RISING | 上升沿 |
| IRQF_TRIGGER_FALLING | 下降沿 |
| IRQF_ONESHOT | threaded IRQ 搭配 |
| IRQF_SHARED | 允許共享 IRQ |

Handler 回傳：

```c
IRQ_HANDLED
IRQ_NONE
```

## 5. Threaded IRQ

Threaded IRQ = 將 interrupt handler 放到 kernel thread 執行。

使用情境：

-   驅動處理時間長  
-   要在 IRQ 裡睡眠
-   e-paper driver（IO sequence）
-   I2C/SPI interrupt-based device
-   Wi-Fi 驅動 often use multiple IRQ threads

示例

```c
request_threaded_irq(irq, top_half, thread_fn,
                     IRQF_ONESHOT, dev_name, dev);
```
執行流程：

```yaml
HW IRQ
   ↓
top_half() → 非常短，通常 return IRQ_WAKE_THREAD
   ↓
thread_fn() 在 kthread 中執行，可睡眠
```

## 6. SoftIRQ（底半部）

SoftIRQ 類型：

| 類別 | 用途 |
|------|------|
| NET_RX | 網路封包接收 |
| NET_TX | 網路送出 |
| TIMER | jiffies 計時 |
| SCHED | scheduler 負責切換 |
| BLOCK | block I/O 底半部 |

SoftIRQ 常以 ksoftirqd CPU thread 執行：

```sh
ksoftirqd/0
ksoftirqd/1
...
```
若這些 thread 負載很高 → 網路延遲上升。

## 7. Tasklet

Tasklet = SoftIRQ 的 API 包裝。

不適合高效能系統（如 Wi-Fi、DRM），因為：

-   只能在同一 CPU 執行
-   不可並行（disable local）

## 8. Interrupt Affinity（綁定 CPU）

例如：

```sh
cat /proc/irq/42/smp_affinity
echo 2 > /proc/irq/42/smp_affinity  # 綁核心 1
```

用途：

-   Wi-Fi 高吞吐 → 綁高效能 CPU  
-   display pipeline → 避免與 networking 共享 IRQ
-   remoteproc IPI → 調整 latency

## 9. 調試命令

查看所有 IRQ：
```sh
cat /proc/interrupts
```
查看中斷控制器：
```sh
cat /sys/kernel/irqchip/*
```
啟用 ftrace 中斷事件：
```sh
echo 1 > /sys/kernel/debug/tracing/events/irq/enable
```
查看 IRQ affinity：
```sh
cat /proc/irq/*/smp_affinity
```

## 10. 中斷延遲（IRQ latency）原因

-   CPU idle → 需要退出 WFI
-   GIC routing 太複雜（尤其 GICv3） 
-   threaded IRQ 被 preempt 
-   softirq backlog 過高
-   cache miss + TLB miss

改善方式：
-   使用 threaded IRQ + IRQF_ONESHOT
-   將 IRQ 綁定到 performance core
-   避免長時間關中斷（preempt off）
-   改善 bottom-half 負載（NAPI、workqueue）

## 11. 常見問題與排查（常見中斷問題與排查）

| 問題 | 可能原因 | 解法 |
|------|-----------|-------|
| IRQ storm | handler 未處理 IRQ source | mask IRQ / clear status register |
| IRQ 無響應 | GIC routing 錯 | 檢查 DTS interrupt-parent |
| shared IRQ stuck | handler 回報 IRQ_NONE | 使用 IRQF_SHARED |
| latency 高 | handler 太長 | threaded IRQ |
| softirq 高負載 | networking RX volume 大 | 調整 NAPI |
