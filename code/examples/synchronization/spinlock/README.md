
# spinlock

Linux kernel 中最基本的「不可睡眠鎖（non-sleeping lock）」範例。

本章的核心觀念：

> **spinlock 是給「不能睡眠」的情境使用的鎖。**

---

## 🎯 本章的目的

理解：

- spinlock 是什麼
- 為什麼不能睡眠
- spinlock vs mutex 的本質差異
- 為什麼 IRQ context 一定要用 spinlock

---

## 🧠 一句話先記住

```
spinlock
= 拿不到就原地忙等（busy wait）
= 不能睡眠
= 適合 IRQ / atomic context
```

## 🔧 本範例做什麼？

-   建立一個 shared_counter
-   用 spinlock 保護
-   在 read / write 中使用

👉 模擬「多執行緒 + 可能被中斷打斷」的情境

----------

## 🧩 Kernel 原始碼對照
```
kernel/locking/spinlock.c  
include/linux/spinlock.h  
arch/*/include/asm/spinlock.h
```
----------

## 🚫 常見錯誤

❌ 在 spinlock 區段裡呼叫：

-   sleep
-   schedule
-   mutex_lock
-   copy_to_user（可能睡）

❌ spinlock 保護太長的區段
