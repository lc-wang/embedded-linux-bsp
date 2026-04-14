
# completion

Linux kernel 中的「一次性事件同步機制」。

本章的核心觀念：

> completion 不是鎖  
> 而是「等待某件事情完成」

---

## 🎯 本章的目的

理解：

- completion 是什麼
- wait_for_completion() 在做什麼
- complete() 如何喚醒等待者
- 為什麼 driver 很常用 completion

---

## 🧠 一句話先記住

```
completion
= 一個 thread 等另一個 thread 說「好了」
```

## 🔧 本範例做什麼？

-   userspace write → 模擬「開始一個工作」
-   kernel 啟動一個 delayed work（延遲 2 秒）
-   userspace read → 等待 completion
-   work 完成後呼叫 complete()

👉 模擬：

-   firmware load 完成
-   DMA 傳輸完成
-   hardware interrupt 完成
