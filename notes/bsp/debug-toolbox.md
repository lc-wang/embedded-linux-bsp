# Kernel Debug Toolbox

> 目的：提供一個**可反覆套用**的 Kernel Debug 入口，讓工程師在 bring-up、線上異常、效能退化時，**用最少成本選對工具、走對路徑**。
> 
> 原則：
> 
> -   先分類「現象」，再選工具（不是反過來）
>     
> -   先觀測、不侵入；最後才改 code / 重編 kernel
>     
> -   每一步都要能**否定假設**（debug 是排除法）
>     

## 1. 快速入口

### 1.1 現象 → 第一工具

| 現象                     | 第一工具                 | 為什麼                                     |
|--------------------------|--------------------------|--------------------------------------------|
| 開機卡住 / 不定 reboot   | pstore / ramoops         | 取得上一輪 crash 的唯一可用證據            |
| 系統 hang 無反應         | Magic SysRq + ftrace     | 判斷是 CPU、IRQ、lock 還是 I/O 問題        |
| Suspend / Resume 失敗    | ftrace（power / sched）  | 需要精確的時序與阻塞點資訊                 |
| 卡頓 / 掉幀              | perf / sched trace       | 優先找出 hot path 與排程瓶頸               |
| 記憶體還有卻被殺         | cgroup / PSI             | 多半是 policy 或 pressure，而非 memory leak|
| Driver 行為異常          | dynamic_debug            | 比 printk 更可控、可針對性開啟             |

> **反例提醒**：
> 
> -   不要第一時間就加 printk
> -   不要一開始就重編 kernel  
> -   不要沒證據就懷疑硬體
>     

## 2. Debug 層級模型

```text
[ User Space ] app / service / framework

│

[ Kernel API ] syscalls / ioctl / netlink

│

[ Kernel Core ] scheduler / mm / irq / power

│

[ Driver ] bus / device / firmware

│

[ HW ] clock / reset / power / signal
```
**責任邊界原則**：

-   上層症狀 ≠ 下層錯誤 
-   Kernel debug 的第一步是：**定位哪一層在違反合約**

## 3. Logging：最低成本、但要節制

### 3.1 printk / dmesg（Baseline）

-   適用：功能是否走到、錯誤碼、一次性事件
-   風險：
    -   時序改變（Heisenbug） 
    -   log 被洗掉 

**實務建議**：
```bash
dmesg -wT
```
-   只在關鍵路徑打
-   一律加明確 prefix（driver / module）

### 3.2 Dynamic Debug（取代亂加 printk）

-   適用：driver / subsystem   
-   優點：runtime 開關、不需重編

#### 查詢可用點

```bash
ls /sys/kernel/debug/dynamic_debug/
```

#### 針對檔案開

```bash
echo 'file drivers/foo/bar.c +p' > /sys/kernel/debug/dynamic_debug/control
```

## 4. Trace：當需要「時間順序」

### 4.1 ftrace / tracefs（核心工具）

**什麼時候一定要用 ftrace？**

-   suspend 卡住
-   poweroff 變 reboot
-   IRQ / sched 問題

```bash
mount -t tracefs nodev /sys/kernel/tracing
```

#### 常用 tracer

-   `function_graph`：看 call flow
-   `sched_switch`：看切換
-   `irq_handler_entry/exit`
```bash
echo function_graph > current_tracer

echo do_suspend+0 > set_ftrace_filter
```
> 關鍵心法：
> 
> -   **先縮範圍，再開 tracer**
> -   trace buffer 不是越大越好

## 5. Crash / Reboot

### 5.1 pstore / ramoops（必開）

-   適用：reboot / panic / watchdog
-   原理：把 crash log 存在 RAM
```bash
ls /sys/fs/pstore/
```
**判斷重點**：

-   panic vs reboot
-   last message 在哪個 subsystem  

### 5.2 Magic SysRq（還活著時）

```bash
echo 1 > /proc/sys/kernel/sysrq
```

#### dump task state

```bash
echo t > /proc/sysrq-trigger
```

## 6. Performance

### 6.1 perf（CPU / Cache / Lock）

```bash
perf top

perf record -g -- sleep 10

perf report
```
**常見誤區**：

-   CPU idle ≠ 沒問題  
-   要搭配 scheduler trace

### 6.2 PSI（Resource Pressure）

```bash
cat /proc/pressure/memory

cat /proc/pressure/cpu
```

## 7. Memory：不是只有 OOM

-   `/proc/meminfo`
-   `/sys/fs/cgroup/*/memory.*`
-   slab / page cache

**關鍵判斷**：

-   reclaim 發生在哪一層
   -   是 kernel pressure 還是 Android policy

## 8. Symbol / Address

### 8.1 kallsyms / vmlinux

```bash
cat /proc/kallsyms | grep foo

addr2line -e vmlinux 0xffffff...
```
-   沒 vmlinux = debug 斷腿    
-   記得保留 build artifacts

## 9. 硬體輔助

-   GPIO toggle（驗證路徑有沒有跑到） 
-   Scope / LA（驗證 timing / power）

> 原則：
> 
> -   軟體證據不足才動硬體

## 10. Debug 決策樹

```text
現象 → 分類 → 最小工具 → 排除假設 → 縮小範圍 → 深入
```

## 11. 敘事

> 「會先判斷是 functional 還是 timing 問題， functional 用 dynamic debug， timing 直接上 ftrace， reboot 則一定先看 pstore， 避免一開始就改 code。」
