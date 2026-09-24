# Linux Notes — Workqueue / Tasklet

> 本章採用與 `memory-model.md` 完全一致的筆記架構：概念 → 原理 → 資料結構 → 執行流程 → 語意 → 使用模式 → 常見錯誤 → Debug → 小結。

## 1. 概念：為何需要 Workqueue / Tasklet？

硬體中斷（IRQ）需要在極短時間內完成處理，但很多工作：

-   可能需要較長時間處理
-   可能需要睡眠（如 I2C/SPI、mutex）  
-   不適合在中斷禁止（IRQ disabled）的情況下執行

因此 Linux 將中斷拆成：

-   **Top Half**（上半部）：快速、不可睡眠，只做最關鍵的工作
-   **Bottom Half**（下半部）：延後較重的工作

Tasklet 與 Workqueue 都屬於 Bottom Half 的實作：

-   **Tasklet**：在 SoftIRQ context 執行 → 不可睡眠
-   **Workqueue**：在 process context 執行 → 可以睡眠

## 2. 背景原理：SoftIRQ 與 Bottom-Half 架構

### 2.1 SoftIRQ

Linux 提供 10+ 種 SoftIRQ，包含 TIMER、NET_RX、SCHED 等。  
Tasklet 即是建立在 SoftIRQ（TASKLET_SOFTIRQ）之上。

**SoftIRQ 特性：**

-   在 interrupt context 執行（不可睡眠）

-   每 CPU 執行流程獨立

-   可能由 `ksoftirqd/*` 

```
IRQ → raise_softirq() → __do_softirq() → 呼叫對應 handler
```

### 2.2 Workqueue

Workqueue 完全不屬於 SoftIRQ，它使用：

-   kernel thread（kworker）執行
-   支援 sleep / scheduling / mutex    
-   可依 CPU 或 unbound 執行
```
schedule_work() → 加入 WQ → kworker picks → 執行 handler
```

## 3. 核心資料結構

### 3.1 tasklet_struct

```c
struct tasklet_struct {
    struct tasklet_struct *next;
    unsigned long state;
    atomic_t count;
    void (*func)(unsigned long);
    unsigned long data;
};

```

-   `func`：回呼函式（不可睡眠） 
-   `state`：TASKLET_STATE_SCHED / RUN
-   `count`：0 表示可執行，非 0 表示被 disable    

### 3.2 work_struct

```c
struct work_struct {
    atomic_long_t data;
    struct list_head entry;
    work_func_t func;
};

```

-   `func`：回呼函式（可睡眠）
-   `entry`：加入 workqueue 的鏈結節點

### 3.3 workqueue_struct

```c
struct workqueue_struct {
    struct list_head worklist;
    unsigned int flags;
    struct pool_workqueue *pwq[];
};

```

-   可為 bound CPU / unbound
-   可能包含多個 worker thread

## 4. 執行流程

### 4.1 Tasklet 流程（SoftIRQ）

```yaml
IRQ Handler
    ↓ tasklet_schedule()
raise TASKLET_SOFTIRQ
    ↓
__do_softirq()
    ↓
tasklet_action()
    ↓
呼叫 tasklet.func(data)
```

-   在 softirq context 執行 → 不可睡眠

### 4.2 Workqueue 流程（kworker）

```yaml
IRQ Handler
    ↓ schedule_work()
將 work 加入 workqueue
    ↓
kworker thread 被喚醒
    ↓
work->func() 在 process context 執行
```

-   可睡眠、可重排程、可阻塞

## 5. 語意與 Context 規則

### 5.1 Tasklet 語意

-   執行於 SoftIRQ → **atomic context** 
-   禁止睡眠（不可呼叫 mutex、msleep、copy_to_user、I2C/SPI）
-   執行時間應短，否則 softirq 壅塞

### 5.2 Workqueue 語意

-   執行於 process context → **可睡眠**
-   適合 heavy work
-   可以呼叫：mutex_lock、msleep、schedule、copy_to_user

### 5.3 Driver 實務法則

**只要你不確定是否會睡眠 → 一律用 Workqueue**  
（Linux 社群官方推薦的方式）

## 6. 使用模式（Patterns）

### 6.1 IRQ → Tasklet（non-sleepable）

```c
irq_handler() {
    tasklet_schedule(&dev->bh);
    return IRQ_HANDLED;
}
```

用於：

-   Network legacy drivers  
-   DMA completion 簡單處理

### 6.2 IRQ → Workqueue（sleepable, most common）

```c
irq_handler() {
    schedule_work(&dev->work);
    return IRQ_HANDLED;
}
```

用於：

-   Wi-Fi RX handle（需要 mac80211 API）
-   DRM Page-flip pipeline（需要 sleep）
-   SPI / I2C device（transfer 必睡眠）

### 6.3 Workqueue + Delayed Work（延遲處理）

```c
schedule_delayed_work(&dev->dwork, msecs_to_jiffies(10));
```

用於：

-   e-ink panel busy polling
-   定期 watchdog

### 6.4 Ordered Workqueue（序列化）

用於：

-   面板初始化流程
-   避免多個 worker 同時操作硬體

## 7. 常見問題與排查（常見錯誤 Pitfalls）

### 7.1 Tasklet 裡睡眠

```
BUG: sleeping function called from invalid context
```

原因：tasklet 在 softirq context

### 7.2 Workqueue race → Use-after-free

若在 module_exit 未 flush：

```c
cancel_work_sync(&work);
flush_workqueue(wq);
```

否則 worker 仍可能存取已釋放的結構 → crash

### 7.3 Tasklet 做太久 → softirq 壅塞

導致：

-   ksoftirqd 佔滿 CPU
-   全系統 latency 升高

### 7.4 使用 system_wq 處理 heavy job

會拖慢其他子系統（網路、調度器）  
→ 應建立專屬 workqueue

## 8. Debug 工具與技巧

### 8.1 SoftIRQ

```
cat /proc/softirqs
```

查看各 softirq 觸發次數

### 8.2 Workqueue DebugFS

```
ls /sys/kernel/debug/workqueue

```

可查看：

-   WQ 配置
-   work item 狀態
-   worker thread 活動

### 8.3 ftrace（觀察 BH 行為）

```
echo function_graph > /sys/kernel/debug/tracing/current_tracer
```

### 8.4 Lockdep（偵測睡眠問題）

```
CONFIG_PROVE_LOCKING=y
```

可立即報錯：sleeping in atomic context
