
# Kernel trace notes — mutex

---

# 🟢 Level 1

如果兩個 thread 同時改同一個變數：

```
thread A: shared_counter += 1
thread B: shared_counter += 1
沒有鎖的話，就可能出現 race condition。
```
mutex 的作用就是：

一次只允許一個人進去改  
其他人先等

----------

# 🟡 Level 2

本範例中：

-   `write()` 會修改 `shared_counter`
-   `read()` 會讀取 `shared_counter`

所以這個變數是：

共享資源

流程如下：
```
userspace write()  
 ↓  
VFS  
 ↓  
driver my_write()  
 ↓  
mutex_lock()  
 ↓  
更新 shared_counter  
 ↓  
mutex_unlock()
```
----------

# 🔴 Level 3
```
mutex_lock()  
 └─ __mutex_lock()  
 └─ 可能睡眠等待  
 ```
```
mutex_unlock()  
 └─ __mutex_unlock_slowpath()
```
重點不是每一層 function 名稱，  
而是你要知道：

拿不到 mutex 的 thread 會睡眠

----------

# 🧠 為什麼 mutex 不能在 IRQ 用？

因為 mutex 的本質是：

拿不到就睡

但 interrupt context 不能睡眠。

所以：

-   process context → 可以 mutex
-   interrupt context → 不可以 mutex

----------


# 與 spinlock 的本質差異  
  
| 項目 | mutex | spinlock |  
|------------|--------------|-----------------|  
| 拿不到鎖 | 睡眠等待 | 原地忙等 |  
| 可否睡眠 | 可以 | 不可以 |  
| 使用情境 | process context | IRQ / atomic context |  
| 臨界區長短 | 可較長 | 必須很短 |

----------

# 🧠 心智模型

mutex 適合保護「會睡眠的共享狀態」  
spinlock 適合保護「不能睡眠的短臨界區」
