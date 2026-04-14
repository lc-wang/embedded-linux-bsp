
# Kernel trace notes — completion

---

# 🟢 Level 1

想像兩個 thread：

```
Thread A：等事情完成
Thread B：事情完成後通知
completion 就是：
```
A: 等  
B: 完成後叫醒 A

----------

# 🟡 Level 2
```
Thread A:  
 wait_for_completion()  
  
 ↓（睡眠）  
  
Thread B:  
 complete()  
  
 ↓  
  
喚醒 Thread A
```
----------

# 🔴 Level 3
```
wait_for_completion()  
 └─ wait_for_common()  
 └─ schedule()  
```
```
complete()  
 └─ wake_up_process()
```
----------


# 🧠 與 mutex 的差異  
  
| 項目 | mutex | completion |  
|----------|------------------|-------------------|  
| 用途 | 保護資源 | 等待事件 |  
| 行為 | lock / unlock | wait / complete |  
| 使用情境 | critical section | async event |

----------

# 🔥 為什麼 driver 很常用？

因為 driver 很多是：
```
start hardware  
 ↓  
等 interrupt  
 ↓  
完成
```
這時候：
```
wait_for_completion()  
+ complete()
```
是最自然的寫法。

----------

# 🧠 心智模型

completion 是「同步兩個時間點」  
不是「保護資料」
