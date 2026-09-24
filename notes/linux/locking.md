# Linux Locking / Synchronization Mechanisms 解析

本章整理 Linux Kernel 中所有同步機制，涵蓋：

- 為何需要同步（race condition）
- 各類鎖的用途與差異
- spinlock/mutex/semaphore/rwsem
- atomic operations
- completion/waitqueue
- seqcount/seqlock
- RCU（Read-Copy-Update）
- interrupt context 與 locking 的搭配
- 何時該用哪一種鎖
- 常見 deadlock 問題與排查方法

## 1. 為何需要同步？

Linux Kernel 是：

- preemptive（可搶佔）
- multi-core（多 CPU 同時執行）
- interrupt-based（隨時可中斷）
- 多 thread（kthread, workqueue, softirq）

因此共享變數容易出現 **race condition**：

CPU0: x = x + 1
CPU1: x = x + 1
→ x 最後可能只 +1 而不是 +2

避免競態條件的工具 → **locking primitives**。

## 2. 鎖的分類總覽

| 類型 | 可睡眠？ | 適用場景 |
|------|---------|-----------|
| **spinlock** | ✗ 不可睡眠 | interrupt context、短臨界區 |
| **mutex** | ✓ 可睡眠 | 長臨界區、不能在 IRQ 使用 |
| **rwlock** | ✗ | 多讀少寫 |
| **semaphore** | ✓ | 舊 API，現在多用 mutex |
| **rwsem** | ✓ | VFS / mm subsystem 常用 |
| **atomic operations** | ✗ | 單變數快速同步 |
| **completion** | ✓ | 等待某事件完成 |
| **waitqueue** | ✓ | block/unblock thread |
| **seqlock** | ✗ | 多讀多寫，讀者不鎖但需 retry |
| **RCU** | ✗ | 高速讀取、延後釋放 |

## 3. Spinlock

適用：

- interrupt context  
- 不可睡眠  
- 臨界區非常短（< 1 µs 最佳）

示例：

```c
spinlock_t lock;

spin_lock(&lock);
/* critical section */
spin_unlock(&lock);
IRQ-safe spinlock
```
```c
spin_lock_irqsave()
spin_unlock_irqrestore()
```
適用於：

interrupt handler 與普通 path 都會使用同一共享資料

## 4. Mutex

適用：
-   可睡眠 context（不能在 IRQ）
-   臨界區較長
-   需要 blocking 行為

使用方法：
```c
struct mutex lock;

mutex_lock(&lock);
/* critical */
mutex_unlock(&lock);
```
若在中斷使用 → 會 kernel panic 或 WARN_ON。

## 5. Semaphore（較舊，已逐漸被 mutex 取代）

適用：
-   需要計數型同步
-   舊 driver 仍可見
示例：
```c
down(&sem);
/* critical */
up(&sem);
```

## 6. RW Semaphore（rwsem）

適用：

-   多讀少寫（典型：VFS & memory subsystem）
```c
down_read()
up_read()
down_write()
up_write()
```
讀不互斥，寫需要獨佔。

## 7. RW Lock（rwlock_t）

適用於 不可睡眠 的多讀少寫場景（與 rwsem 的差別：不可睡眠）

```c
read_lock()
write_lock()
```

多用於:
-   networking
-   softirq
-   fast path

## 8. Atomic Operations（最快但最簡單）

適用：
-   單一整數（counter）  
-   lock-free operation

示例：

```c
atomic_inc(&v);
atomic_dec(&v);
atomic_read(&v);
```
不能保護複雜結構。

## 9. Completion

適用：
-   等待某事件完成（如 firmware loading, workqueue 完成）  

例：

```c
struct completion done;

wait_for_completion(&done);
complete(&done);
```
比 waitqueue 更簡單。

## 10. Waitqueue

適用：
-   等待 condition 變為 true    
-   實作阻塞式讀寫很常用

例
```c
wait_event_interruptible(wq, flag == 1);
```
喚醒：
```c
wake_up(&wq);
```

## 11. Seqcount / Seqlock

專門給：

-   **多讀、多寫** 
-   讀者不加鎖
-   但讀者需要 retry

流程
```c
writer lock
writer++, update data
writer unlock

reader:
 do {
   seq = read_seqbegin()
   read data
 } while (read_seqretry())
 ```

非常快，用於：
-   jiffies
-   timekeeping 
-   networking data path

## 12. RCU（Read-Copy-Update）

RCU 是 Linux 中最重要的 lock-free 讀取機制之一。
適用：

-   讀者極度頻繁    
-   寫者少但可容忍延時 
-   大量讀取 + 低成本讀操作
核心理念：

```yaml
Writer：複製 → 修改 → 替換 → 延遲釋放舊資料
Reader：不加鎖直接讀
```
常見 API：

```c
rcu_read_lock()
rcu_read_unlock()

list_for_each_entry_rcu()
rcu_assign_pointer()
synchronize_rcu()
```

使用場景：
-   networking    
-   VFS path lookup
-   binder
-   task structure traversal

## 13. Interrupt Context vs Locking

| 鎖類型 | IRQ context 可用？ |
|--------|---------------------|
| spinlock | ✓（需 irqsave） |
| mutex | ✗ |
| semaphore | ✗ |
| rwsem | ✗ |
| atomic | ✓ |
| seqlock | ✓ |
| RCU | ✓（讀取） |

記憶方式：

> IRQ context 只能用 non-sleeping primitives。

## 14. 如何選擇正確的鎖？

✓ 如果在 interrupt context → 用 spinlock
✓ 如果臨界區很短 → 用 spinlock
✓ 如果臨界區會睡眠 → 用 mutex
✓ 多讀少寫 + 可睡眠 → 用 rwsem
✓ 多讀少寫 + 高速 + 不可睡眠 → 用 rwlock
✓ 共享整數 → atomic
✓ 多 reader、writer 少 → 用 RCU
✓ 讀者不加鎖 + 容忍 retry → seqlock

## 15. Debug 工具

查看鎖等待
```sh
echo w > /proc/sysrq-trigger
```
ftrace 追蹤鎖行為
```sh
echo function > /sys/kernel/debug/tracing/current_tracer
echo "*lock*" > set_ftrace_filter
```
lockstat 分析
```sh
lockstat
```
可分析：

-   哪個鎖最常被 contention    
-   誰等待時間最久

## 16. 常見問題與排查（Deadlock 常見原因）

| 問題 | 說明 |
|------|------|
| A → B → A 迴圈 | 鎖順序錯誤 |
| 同一鎖重複加鎖 | spinlock recursion |
| 在 spinlock 中睡眠 | 常見錯誤 |
| IRQ handler 取得已被 thread 鎖住的 spinlock | 需使用 irqsave |
| RCU reader 持續太久 | writer 永遠無法釋放 |

利用 lockdep 偵測：
```sh
echo 1 > /proc/sys/kernel/debug/lockdep
```
