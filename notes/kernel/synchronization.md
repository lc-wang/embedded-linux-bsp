# Linux Synchronization Mechanisms

這份筆記整理 Linux kernel 常見的同步 (synchronization) 機制。  
目標是理解各種鎖與同步原語的用途、限制與使用情境。

## 1. 為什麼需要同步？

- Kernel 同時可能有多個 CPU 核心或中斷在存取共享資源。  
- 若沒有同步機制，容易產生 **race condition** 或 **資料不一致**。  
- 典型情境：
  - 中斷與 process context 同時更新變數。
  - 多個核心同時操作同一暫存器。
  - 多執行緒共用緩衝區。

## 2. 原子操作 (Atomic Operations)

- 適用於簡單變數的原子更新（例如計數器、flag）。  
- 由 CPU 指令層面保證原子性，不需加鎖。  
- 常用 API：
```c
atomic_t counter;
atomic_inc(&counter);
atomic_dec(&counter);
atomic_read(&counter);
atomic_cmpxchg(&counter, old, new);
```
-   優點：開銷小、不需進入臨界區。
-   缺點：僅限簡單整數操作。

## 3. 自旋鎖 (Spinlock)

-   用於 **短暫臨界區**，不可睡眠。
-   適合中斷 handler 或高頻操作。
-   範例：
```c
spinlock_t lock;
spin_lock_init(&lock);
spin_lock(&lock); 
/* critical section */ 
spin_unlock(&lock);
``` 

-   變體：

    -   `spin_lock_irqsave()` / `spin_unlock_irqrestore()`：保護中斷 context。
    -   `rwlock_t`：多讀單寫。

-   注意：**禁止在 spinlock 內睡眠**。

## 4. Mutex

-   適用於 **process context**、允許睡眠。
-   不能在中斷或 atomic context 使用。
-   範例：
```c
struct  mutex  my_lock;
mutex_init(&my_lock); 
mutex_lock(&my_lock); 
/* critical section */ 
mutex_unlock(&my_lock);
``` 

-   若嘗試在中斷中使用 mutex → kernel 會警告 “sleeping function called from invalid context”。

## 5. Semaphore

-   與 mutex 類似，但允許同時多個 holder。
-   通常用於「可重入資源」或舊式驅動。
```c
struct  semaphore  sem;
sema_init(&sem, 1); // binary semaphore 
down(&sem); 
/* critical section */ 
up(&sem);
``` 

## 6. Completion

-   用於 **事件通知**：一方等待、另一方完成後喚醒。
-   類似於 user space 的 condition variable。
-   範例：
```c
DECLARE_COMPLETION(my_comp); 
// 在 worker thread 中 
complete(&my_comp); 
// 在等待端 
wait_for_completion(&my_comp);
```
-   適用於：初始化完成、I/O 完成通知。

## 7. Wait Queue

-   適用於 **阻塞等待某條件成立**。
-   範例：
```c
DECLARE_WAIT_QUEUE_HEAD(wq);
wait_event_interruptible(wq, condition_is_true);
wake_up_interruptible(&wq);
```
-   通常搭配中斷或工作佇列使用。

## 8. Read-Copy-Update (RCU)

-   適合 **讀多寫少** 的資料結構，例如路由表或設備列表。   
-   原理：寫入時建立新版本，等待讀者完成後再釋放舊資料。
-   範例：
```c
rcu_read_lock();
p = rcu_dereference(ptr); 
/* use p */ 
rcu_read_unlock();
rcu_assign_pointer(ptr, newp);
synchronize_rcu();
```
-   優點：讀取無鎖，效能極佳。
-   缺點：寫入邏輯較複雜。

## 9. 工作佇列 (Workqueue)

-   讓工作從中斷延後至 process context 執行，可睡眠。

-   範例：
```c
static struct work_struct my_work;

static void my_work_func(struct work_struct *work)
{
    pr_info("work executed\n");
}

INIT_WORK(&my_work, my_work_func);
schedule_work(&my_work);
```
-   適合延遲任務或需要睡眠的操作。

## 10. Debug 工具

| 工具 / 選項 | 功能用途 |
| --- | --- |
| `CONFIG_LOCKDEP` | 鎖依賴偵測機制，可在編譯時啟用以偵測死鎖風險 |
| `CONFIG_PROVE_LOCKING` | 驗證鎖的使用是否符合規範（例如不可在中斷中用 mutex） |
| `ftrace` / `trace-cmd` | 追蹤鎖的競爭與函式呼叫延遲 |
| `/proc/locks` | 顯示目前被持有的鎖資訊 |
| `perf lock` | 分析鎖競爭熱點與鎖等待時間統計 |
| `lockstat` | 追蹤鎖使用頻率與延遲（需 `CONFIG_LOCK_STAT`） |

## 11. 常見問題與排查（常見陷阱）

| 錯誤行為 | 問題說明 | 修正建議 |
| --- | --- | --- |
| 在 spinlock 內呼叫可睡眠函式 | 會導致 kernel warning 或死鎖（例如呼叫 `msleep()`、`mutex_lock()`） | 使用 mutex 或將任務移至工作佇列 (workqueue) |
| 忘記釋放鎖 | 造成其他執行緒永久阻塞，甚至系統 hang 住 | 在 error path、return 前確保解鎖 |
| 鎖類型使用錯誤 | 效能不佳或中斷被長時間阻塞 | 根據情境選擇合適的同步原語（spinlock/mutex/rcu） |
| 鎖順序不一致 | 兩個鎖互相等待 → 發生死鎖 | 統一鎖的 acquire 順序，保持全域一致性 |
| 在中斷 context 使用 mutex/semaphore | 會睡眠導致「sleeping in atomic context」錯誤 | 中斷內只可使用 spinlock 或 atomic 變數 |

## 12. 學習建議

1.  先從 `spinlock`、`mutex` 開始練習。
2.  寫一個有 race condition 的例子，再加入鎖修正。
3.  觀察鎖造成的延遲，用 `ftrace` 分析差異。
4.  練習 `wait_queue` 或 `completion` 模式，模擬中斷通知。
5.  深入研究 RCU：閱讀 `kernel/rcu/` 原始碼與文檔。

## 附錄

**延伸閱讀**

-   `Documentation/locking/`
-   `Documentation/core-api/atomic_ops.rst`
-   `Documentation/RCU/whatisRCU.rst`
