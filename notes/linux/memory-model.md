
# Linux Memory Model / SMP Ordering / Barrier / Acquire-Release 解析

本章介紹 Linux 在多核心（SMP）架構下的記憶體一致性模型，包括：

- 為何需要記憶體序
- CPU reorder（亂序執行）
- ARM weak memory model
- compiler barrier / CPU barrier / SMP barrier 的差異
- smp_mb(), smp_rmb(), smp_wmb()
- acquire/release 語義
- atomic operations memory ordering
- lock/unlock 的隱含 barrier
- 驅動中常見 memory ordering bug
- debug 工具與如何分析 ordering 問題

---

# 1. 為何需要 Memory Ordering？

在 multi-core 系統中：

- CPU 可以重排記憶體存取指令  
- Compiler 也會重排程式碼  
- DMA 也不保證順序  
- ARM 架構的 memory ordering 比 x86 更弱  

因此，對共享變數操作時會產生 race condition。

示例：
```c
CPU0: x = 1;
CPU0: flag = 1;

CPU1:
while (!flag) {}
print(x);
```
**以為**一定印出：
```yaml
1
```

但在 ARM 上可能印出：
```yaml
0
```

因為 CPU0 很可能「先寫 flag，再寫 x」。

---

# 2. CPU Reorder（硬體重排）

ARM 的特性：

- store 可以往後/前重排  
- load 可以與其他 load/store 重排  
- x86 比較強一致性，但 ARM weak model 允許更多 reorder  

示例：
```yaml
STORE x=1
STORE flag=1
```
可能會變成：
```yaml
STORE flag=1
STORE x=1
```
這就是 memory barrier 的用途。

---

# 3. Compiler Reorder（編譯器重排）

編譯器基於最佳化也會：

- 將程式碼順序交換  
- 提前計算  
- 延後計算  

防止方式：`barrier()` 或 volatile（但 volatile 在 kernel 幾乎不用）

---

# 4. Memory Barrier 類型

| 類型 | 作用範圍 | 使用方式 |
|------|----------|-----------|
| **compiler barrier** | 阻止 compiler 重排 | `barrier()` |
| **CPU barrier** | 阻止 CPU pipeline 重排 | `mb(), rmb(), wmb()` |
| **SMP barrier** | 防止在多核心間重排 | `smp_mb(), smp_rmb(), smp_wmb()` |

記法：
```yaml
smp_mb() = multiprocessor full barrier
```
---

# 5. Linux memory barrier API

## 5.1 Full barrier：smp_mb()
```yaml
smp_mb();
```
用途：

- 確保前後的所有 load/store 都不會重排  
- 最嚴格的 barrier  

典型使用：
```yaml
x = 1;
smp_mb();
flag = 1;
```
保證寫 x 必定先發生。

---

## 5.2 Read barrier：smp_rmb()
```yaml
smp_rmb();
```
用途：
- 防止 read-read 或 read-write 重排

例如：
```yaml
while (!flag)
smp_rmb();

val = x;
```
---

## 5.3 Write barrier：smp_wmb()
```yaml
smp_wmb();
```
用途：

- 防止 write-write 或 write-read 重排

例如（producer code）：
```yaml
buffer[i] = data;
smp_wmb();
ready[i] = 1;
```
---

# 6. Acquire / Release 語義（現代 Linux 建議用法）

現代 Linux 更鼓勵使用：
```yaml
smp_store_release()
smp_load_acquire()
```

範例：
```yaml
smp_store_release(&flag, 1); // 確保 flag 前所有 store 都生效
...
if (smp_load_acquire(&flag)) {
// 所有 reload data 保證可見
}
```
優點：

- 比 smp_mb() 更便宜  
- 語意更清楚  

---

# 7. Atomic Operation Memory Ordering

原子操作也具有 ordering：

| 操作 | 語義 |
|------|------|
| atomic_xxx() | no barrier |
| atomic_xxx_relaxed | no ordering |
| atomic_xxx_release | release barrier |
| atomic_xxx_acquire | acquire barrier |
| atomic_xxx_acquire_release | full barrier |

例如：
```yaml
atomic_set_release(&flag, 1);
```
代表：

> 此 store 之前的所有 memory write 不得被 reorder 到此 store 之後。

---

# 8. Lock 的隱含 Barrier

以下操作天然包含 full barrier：

| 操作 | 類型 |
|------|------|
| spin_lock() / spin_unlock() | full barrier |
| mutex_lock() / mutex_unlock() | full barrier |
| rwsem | acquire/release barrier |
| RCU read lock | acquire |
| RCU write unlock | release |

因此如果你 already 在 lock/unlock 中，不需要額外 smp_mb()。

---

# 9. 常見錯誤

## 9.1 Writer 先寫 flag，再寫 data

錯誤：
```yaml
data = 123;
flag = 1;
```

在 ARM 上可能 reorder。

修正：
```yaml
data = 123;
smp_wmb();
flag = 1;
```

---

## 9.2 Reader 看到 flag，但看不到資料
```yaml
while (!flag);

print(data);
```
可能印出舊值。

修正：
```yaml
while (!smp_load_acquire(&flag));
print(data);
```
---

## 9.3 multi-producer multi-consumer queue 發生 race

解法：

- 使用 RCU  
- 或使用 seqcount/seqlock  
- 或使用 atomic acquire/release  

---

# 10. 與 DMA 的 Memory Ordering

DMA 與 CPU 不共享 cache 一致性（除非硬體支援）。

常見錯誤：
```yaml
cpu writes buffer
dma reads buffer → 得到舊資料
```
原因：

- CPU cache 還沒 flush  
- DMA 在讀舊的 memory

解法：
```yaml
dma_sync_single_for_device()
dma_sync_single_for_cpu()
```
---

# 11. Debug Memory Ordering

### ftrace（追蹤 load/store）
```sh
trace-cmd record -e kmem:* -e sched:* -e irq:*
```

### KCSAN（Kernel Concurrency Sanitizer）

Linux 支援：
```yaml
CONFIG_KCSAN=y
```
可偵測 race condition。

