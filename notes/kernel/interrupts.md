# Linux Interrupts (中斷基礎筆記)

這份筆記整理 Linux kernel 驅動中常見的中斷相關知識與使用方式。

## 1. 什麼是中斷？

- **中斷 (Interrupt)**：硬體在需要 CPU 注意時，向處理器發出訊號。  
- **目的**：避免 CPU 忙等 (polling)，提高效能。  
- **例子**：
  - 鍵盤按鍵事件
  - 網卡收到封包
  - WiFi 模組完成掃描
  - 計時器到期

## 2. Linux 中斷處理流程

1. **硬體觸發中斷線 (IRQ)**  
2. CPU 進入 **中斷上下文 (interrupt context)**  
3. Kernel IRQ subsystem 呼叫對應的 handler (ISR)  
4. 驅動程式處理事件，或排程工作到底半部 (bottom half)

## 3. 上半部 / 下半部

- **上半部 (Top half)**  
  - 短小、快速  
  - 在 **中斷上下文** 中執行（不可睡眠）  
  - 只做必要動作，例如讀暫存器、排程工作
- **下半部 (Bottom half)**  
  - 在 process context 或 softirq 中執行（可睡眠）  
  - 負責較耗時的工作（例如封包處理）

常見下半部機制：
- **tasklet**
- **workqueue**

## 4. 註冊中斷 Handler

```c
#include <linux/interrupt.h>

static irqreturn_t my_isr(int irq, void *dev_id)
{
    pr_info("Interrupt handled: irq=%d\n", irq);
    return IRQ_HANDLED;
}

int ret = request_irq(irq_number, my_isr, IRQF_SHARED, "my_device", dev_id);

if (ret) {
    pr_err("Failed to request IRQ %d\n", irq_number);
}
```
- request_irq() 參數：
  - irq_number：中斷號
  - handler：ISR function
  - flags：常見 IRQF_SHARED (共享中斷線)
  - name：在 /proc/interrupts 中顯示的名字
  - dev_id：共享中斷時用來區分不同裝置

- 釋放中斷：
```c
free_irq(irq_number, dev_id);
```

## 5. IRQ Thread 化

- 某些驅動不希望在硬中斷中處理太多事，可以使用 threaded IRQ。
```c
request_threaded_irq(irq, my_isr, my_thread_fn,
                     IRQF_ONESHOT, "my_device", dev_id);
```
- my_isr：top half，若回傳 IRQ_WAKE_THREAD 會喚醒 thread
- my_thread_fn：bottom half，可在 thread context 執行（可睡眠）

## 6. Device Tree 與 IRQ

- 在 Device Tree 中描述中斷：

```c
my_device@0 {
    compatible = "vendor,mydevice";
    reg = <0x10>;
    interrupt-parent = <&gic>;
    interrupts = <5 IRQ_TYPE_LEVEL_HIGH>;
};
```
- 驅動中取得 IRQ：
```c
int irq = platform_get_irq(pdev, 0);
request_irq(irq, my_isr, 0, dev_name(&pdev->dev), dev);
```

## 7. 常見 Debug 方法

- 查看系統 IRQ 分佈：
```c
cat /proc/interrupts
```
- 驗證驅動 ISR 是否觸發：
```c
dmesg | grep my_device
```
- ftrace 追蹤：
```c
echo function > /sys/kernel/debug/tracing/current_tracer
echo my_isr > /sys/kernel/debug/tracing/set_ftrace_filter
```

## 8. 常見問題與排查（常見注意事項）

- ISR 不可睡眠（不可呼叫可能 block 的 API，如 msleep、mutex_lock）。
- ISR 應短小，把耗時動作放到 workqueue 或 threaded IRQ。
- 若裝置可能共享中斷，要用 IRQF_SHARED 並檢查來源。
- 驅動卸載時記得 free_irq()，否則會造成 crash。
