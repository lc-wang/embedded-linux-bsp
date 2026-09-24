# Linux Kernel Debug Tools Overview

本章整理 Linux Kernel 中常用的除錯工具與技術，  
涵蓋從驅動開發、排查死鎖、性能分析到記憶體洩漏偵測等實用方法。  
所有工具皆為內建或主流開源工具，適用於 Android / Linux BSP / 裸機環境。

## 1. printk 與 dmesg（最基本的 Debug）

打印訊息仍然是 kernel 除錯的第一步。

### 1.1 常用等級

| 等級 | 說明 |
| --- | --- |
| `KERN_ERR` | 嚴重錯誤 |
| `KERN_WARNING` | 警告訊息 |
| `KERN_INFO` | 一般訊息 |
| `KERN_DEBUG` | 除錯訊息 |

### 1.2 查看 Kernel Log

```bash
dmesg | grep my_driver
logcat -b kernel    # Android
```

## 2. debugfs

提供大量 kernel 狀態與 debug 介面。

掛載方式：
```bash
mount -t debugfs none /sys/kernel/debug
```

### 2.1 常見 Debug 節點

| 路徑 | 功能 |
|------|------|
| `/sys/kernel/debug/tracing/` | ftrace 主入口 |
| `/sys/kernel/debug/gpio/` | GPIO 狀態 |
| `/sys/kernel/debug/clk/clk_summary` | clock tree 摘要 |
| `/sys/kernel/debug/regmap/` | 驅動中的 regmap 診斷 |

## 3. ftrace（最強大的內建追蹤器）

可追蹤 function call、latency、sched、irq 等。

啟用 function tracer：

```bash
echo function > /sys/kernel/debug/tracing/current_tracer
cat /sys/kernel/debug/tracing/trace
```
追蹤特定函式：

```bash
echo my_function > set_ftrace_filter
```
trace-cmd（ftrace 封裝工具
```bash
trace-cmd record -p function -l my_function
trace-cmd report
```

## 4. perf（性能分析）

可分析 CPU 使用率、callgraph、cache miss、hotspot。

安裝（Android）：

```bash
adb shell perf list
```
範例：
```bash
perf top
perf record -g -- my_app
perf report
```

### 4.1 常用事件

| 事件 | 說明 |
|-------|------|
| `cpu-clock` | CPU 取樣 |
| `sched:*` | 排程事件 |
| `irq:*` | 中斷事件 |
| `kmalloc` | 記憶體分配 hot spot |

## 5. lockdep（鎖定偵測）

偵測 kernel deadlock / recursive lock / inconsistent lock order。

啟用：

```text
CONFIG_LOCKDEP = y
CONFIG_PROVE_LOCKING = y
```
Kernel log 若發現：
```pgsql
WARNING: possible recursive locking detected
```
代表鎖使用順序錯誤。

## 6. kmemleak（記憶體洩漏偵測）

類似 GC 標記，找出無法回收的 kernel 記憶體物件。

啟用：
```bash
echo scan > /sys/kernel/debug/kmemleak
cat /sys/kernel/debug/kmemleak
```
可用於：
-   驅動未 free 的 kmalloc
-   未解除的 DMA map
-   vmalloc memory leak

## 7. dynamic debug（控制 printk）

只想動態開啟特定模組的 debug log 時使用。

列出所有可調項目：
```bash
cat /sys/kernel/debug/dynamic_debug/control
```
啟用某檔案 debug：

```bash
echo "file my_driver.c +p" > control
```

## 8. crash / kdump（分析 Kernel Panic Dump）

觸發 kernel crash dump：

```bash
echo c > /proc/sysrq-trigger
```
使用 crash 工具：

```bash
crash vmlinux vmcore
```
可分析：
-   backtrace
-   task list
-   slab
-   memory usage
-   locks

## 9. kgdb / gdbstub（Kernel 斷點）

在嵌入式與 Android 上也可使用（串口 / USB）。

啟用：
```text
CONFIG_KGDB=y
CONFIG_KGDB_SERIAL_CONSOLE=y
```
連線：
```bash
gdb vmlinux
target remote /dev/ttyUSB0
```
支援：
-   下斷點
-   單步執行
-   查寄存器
-   查看變數

## 10. 常用 Debug API

| API | 功能 |
|------|------|
| `WARN()` | 顯示 warning 並 dump stack |
| `WARN_ON(condition)` | 若條件成立則警告 |
| `BUG()` | 立即崩潰（慎用） |
| `dump_stack()` | 輸出 call trace |
| `print_hex_dump()` | Dump buffer 內容 |

## 11. 何時使用什麼工具？

| 問題類型 | 推薦工具 |
|-----------|-----------|
| Kernel panic / crash | crash, kdump |
| latency、卡頓 | ftrace, perf |
| driver 不運作、不進入 probe | dmesg, dynamic debug |
| 記憶體洩漏 | kmemleak |
| 鎖死 / 卡住 | lockdep, ftrace (sched) |
| boot hang | printk, earlycon, ftrace, kdump |
