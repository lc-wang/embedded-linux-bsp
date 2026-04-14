# waitqueue

Linux kernel 中最通用的「阻塞等待機制」。

本章核心觀念：

> waitqueue 不是鎖  
> 而是「讓 thread 睡，直到條件成立」

---

## 🎯 本章目的

理解：

- wait_event() 在做什麼
- wake_up() 如何喚醒等待者
- waitqueue 與 completion 的差異
- driver 如何實作 blocking read

---

## 🧠 一句話先記住

```
waitqueue
= 等「某個條件成立」
```

## 🔧 本範例做什麼？

-   userspace read → 如果沒有資料，就睡
-   userspace write → 寫入資料 + 喚醒 reader

👉 模擬：

-   pipe / socket
-   input device
-   event queue
-   driver data ready

----------

## 🧩 Kernel 原始碼對照
```
kernel/sched/wait.c  
include/linux/wait.h  
fs/read_write.c
```
