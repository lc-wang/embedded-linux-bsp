
# Kernel trace notes — spinlock


# 🟢 Level 1

mutex：

```
拿不到鎖 → 去睡覺
```
spinlock：
```
拿不到鎖 → 原地一直轉（忙等）
```
----------

# 🟡 Level 2
```
spin_lock()  
 ↓  
檢查鎖是否被持有  
 ↓  
如果有 → 一直 loop（spin）  
 ↓  
拿到鎖 → 進入 critical section
```
----------

## 🧠 為什麼叫 spin？

因為 CPU 會這樣：

while (lock_taken)  
 ;

👉 一直「空轉」

----------

# 🔴 Level 3
```
spin_lock()  
 └─ raw_spin_lock()  
 └─ arch_spin_lock()  
 └─ CPU atomic 指令
```
----------

# 🔥 spin_lock_irqsave 在幹嘛？
```
spin_lock_irqsave(&lock, flags);
```
做兩件事：

1. 關閉 local interrupt  
2. 拿 spinlock

----------

# 🧠 為什麼要關中斷？

避免這種情況：

CPU0 拿 lock  
→ 被 interrupt 打斷  
→ interrupt handler 也想拿 lock  
→ deadlock

----------

# 🚫 絕對禁止

在 spinlock 區段內：

❌ copy_to_user  
❌ schedule  
❌ mutex_lock  
❌ msleep

因為：
```
spinlock 區段不能睡眠
```
----------


# 🔥 mutex vs spinlock

| 項目           | mutex     | spinlock        |
|----------------|-----------|-----------------|
| 拿不到鎖       | 睡眠      | busy wait       |
| 可否睡眠       | ✅        | ❌              |
| 使用 context   | process   | IRQ / atomic    |
| 臨界區長度     | 可長      | 必須極短        |

----------

# 🧠 心智模型

spinlock 是為了「不能睡眠的世界」而存在
