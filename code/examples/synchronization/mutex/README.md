
# mutex

Linux kernel 中最基本的睡眠型鎖（sleeping lock）範例。

本章的目的是建立一個很重要的觀念：

> mutex 不是用來「保護程式碼」而已，
> 而是用來保護「可能睡眠、可被多執行緒同時碰觸的共享資源」。

---

## 🎯 本章的目的

理解以下問題：

- mutex 是什麼
- 什麼情況要用 mutex
- mutex 與 spinlock 的差異
- 為什麼 mutex 不能在 interrupt context 使用

---

## 🧠 一句話先記住

```
mutex
= 可以睡眠的鎖
= 適合 process context
= 不適合 IRQ / atomic context
```

## 🔧 本範例做什麼？

此範例建立一個共享計數器 `shared_counter`：

-   `write()` 會拿 mutex、更新計數器
-   `read()` 會拿 mutex、讀出目前值

目的是模擬：

-   多個 userspace process 同時打開同一個 device
-   driver 內部需要保護共享狀態

----------

## 🧩 Kernel 原始碼對照
```
kernel/locking/mutex.c  
include/linux/mutex.h  
fs/read_write.c
```
----------

## 🧠 何時該用 mutex？

適合以下情境：

-   保護 driver private data
-   保護 register shadow state
-   保護 buffer queue / 狀態機
-   保護 open / close / ioctl / read / write 共用資料

----------

## 🚫 何時不能用 mutex？

不能在以下 context 使用：

-   interrupt handler
-   softirq
-   tasklet
-   spinlock 持有期間
-   任何不能睡眠的 atomic context
