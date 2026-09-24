# Linux / Android Tracing & Debug Toolchain

## 1. 為什麼是 Toolchain，而不是 Tool List

實務上的 debug 從來不是：

>「選一個工具，把它用到極致」

而是：

>「**一條觀測鏈**：從現象 → 層級 → 工具 → 下一步決策」

- 單一工具永遠不夠  
- 問題常跨 userspace / kernel / firmware  
- Debug 成敗取決於 **第一刀是否切在正確層級**

因此本章以 **工程決策模型（Engineering Decision Model）** 組織。

## 2. Debug 工具全景地圖（System View）

```text
Userspace behavior
 ├─ strace / ltrace
 ├─ logcat / atrace / systrace
 └─ simpleperf

Syscall / IPC boundary
 ├─ perf trace
 ├─ ftrace (sys_enter / sys_exit)
 └─ Binder tracepoints

Kernel control path
 ├─ ftrace (function / function_graph)
 ├─ tracepoints
 ├─ dynamic_debug
 └─ printk / trace_printk

Kernel data path / latency
 ├─ perf (sched / irq / lock)
 ├─ eBPF / bpftrace
 └─ BCC tools

Scheduler / IRQ / Locking
 ├─ sched_switch / sched_wakeup
 ├─ irqsoff / preemptoff
 ├─ lockdep
 └─ RT throttling

Memory / Resource pressure
 ├─ vmstat / slabtop
 ├─ PSI
 ├─ kmemleak
 └─ KASAN / UBSAN

Crash / Hang / Live kernel
 ├─ addr2line / objdump / nm
 ├─ crash utility
 ├─ GDB / KGDB
 ├─ netconsole
 └─ magic SysRq
```
第一步不是開工具，而是定位層級。

## 3. 工程決策模型：問題 → 層級 → 工具

```text
App 卡住、無 crash
→ strace / logcat / atrace

ioctl / binder timeout
→ strace → ftrace (syscall)

driver probe / suspend 卡住
→ ftrace (function_graph) → dynamic_debug

系統 lag、CPU usage 正常
→ perf sched → PSI

IRQ latency 異常
→ perf irq → eBPF

random kernel crash
→ dmesg → addr2line → crash

系統完全 hang
→ SysRq / netconsole → KGDB
```
這張對照表就是 **debug 的入口點**。

## 4. Toolchain 接力流程（Debug Flow）

### 4.1 範例：Wi-Fi 偶發 connect timeout

```text
strace
 → ioctl 阻塞

ftrace
 → driver control path 停住

perf sched
 → workqueue latency

addr2line
 → 對應實際 code

（必要時）KGDB
 → 驗證 lock / state
```
**重點不是工具，而是「順序」。**

## 5. Userspace / Android Debug

### 5.1 strace — App / HAL 為什麼卡住？

**使用時機**

-   無 crash、畫面 freeze

-   CPU usage 不高

`strace -tt -T -f -p <pid>` 

判斷：

-   `futex()` → scheduler / locking

-   `ioctl()` → driver / Binder

若卡在 ioctl，下一步進 **Syscall / Kernel 邊界**

### 5.2 ltrace — 排除 userspace library 問題

`ltrace -tt -T -p <pid>` 

用途：

-   驗證是否卡在 libc / vendor lib

-   先證明「不是 kernel」

### 5.3 logcat — Android service / Binder 行為

`logcat -b system -b main -v time` 

用途：

-   ANR

-   Binder transaction timeout

### 5.4 atrace / systrace — Framework 與 scheduler 互動

`atrace -b 4096 -t 10 sched freq idle binder_driver` 

用途：

-   Binder thread starvation

-   scheduler latency

### 5.5 simpleperf — Android native code 熱點

```bash
simpleperf record -p <pid>
simpleperf report
```

用途：

-   userspace hotspot

-   不干擾 kernel

## 6. Syscall / IPC Boundary Debug

### 6.1 perf trace — syscall latency 分佈

`perf trace -p <pid>` 

用途：

-   比 strace 低干擾

-   看 syscall timing 是否異常

### 6.2 Binder tracepoints — Binder transaction 追蹤

```bash
echo 1 > /sys/kernel/debug/tracing/events/binder/enable 
cat /sys/kernel/debug/tracing/trace_pipe
```
用途：

-   Binder call 是否送出 / 回來

-   Android framework debug 核心工具

## 7. Kernel Control Path Debug

### 7.1 ftrace (function) — code 是否被呼叫

```bash
echo  function > /sys/kernel/debug/tracing/current_tracer 
echo my_driver_* > /sys/kernel/debug/tracing/set_ftrace_filter
```

### 7.2 ftrace (function_graph) — 哪個 callback 卡住

`echo function_graph > /sys/kernel/debug/tracing/current_tracer` 

常用於：

-   probe

-   suspend / resume

### 7.3 tracepoints — 精準觀察 subsystem

```bash
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable
```

### 7.4 dynamic_debug — 精準開 log

```bash
echo  'file drivers/net/wireless/* +p' \
 > /sys/kernel/debug/dynamic_debug/control 
```
用途：

-   避免 printk flood

-   bring-up 必備

### 7.5 printk / trace_printk — early boot / 無 tracing

`trace_printk("reach here\n");` 

## 8. Performance / Latency Analysis

### 8.1 perf record / report — CPU hotspot

```bash
perf record -a
perf report
```

### 8.2 perf sched — runnable 卻沒跑

```bash
perf sched record -a
perf sched latency
```

### 8.3 perf irq — IRQ latency

`perf record -e irq:irq_handler_entry -a` 

### 8.4 eBPF — runtime probe

`bpftool prog list` 

### 8.5 bpftrace — 快速一次性分析

```text
bpftrace -e 'tracepoint:sched:sched_switch { @[comm] = count(); }'
```

### 8.6 BCC tools — 現成工具

`runqlat` 

## 9. Scheduler / IRQ / Locking Analysis

### 9.1 sched_switch / sched_wakeup

```bash
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable 
```

### 9.2 irqsoff — IRQ 被關太久

`echo irqsoff > /sys/kernel/debug/tracing/current_tracer` 

### 9.3 preemptoff — preempt latency

`echo preemptoff > /sys/kernel/debug/tracing/current_tracer` 

### 9.4 lockdep — 死鎖 / lock inversion

`echo 1 > /proc/sys/kernel/lockdep` 

### 9.5 RT throttling

`cat /proc/sys/kernel/sched_rt_runtime_us` 

## 10. Memory / Resource Debug

### 10.1 vmstat

`vmstat 1` 

### 10.2 slabtop

`slabtop` 

### 10.3 PSI — resource stall

`cat /proc/pressure/cpu cat /proc/pressure/memory` 

### 10.4 kmemleak

```bash
echo scan > /sys/kernel/debug/kmemleak 
cat /sys/kernel/debug/kmemleak
```

### 10.5 KASAN / UBSAN

```text
CONFIG_KASAN=y
CONFIG_UBSAN=y
```

## 11. Crash / Hang / Live Kernel Debug

### 11.1 addr2line

`addr2line -e vmlinux <addr>` 

### 11.2 objdump / nm

`nm vmlinux | grep <symbol>` 

### 11.3 crash utility

`crash vmlinux vmcore` 

### 11.4 GDB

`gdb vmlinux` 

### 11.5 KGDB — 最後手段

`echo g > /proc/sysrq-trigger` 

### 11.6 netconsole — serial 也掛了

`modprobe netconsole netconsole=@/,@<host-ip>/` 

### 11.7 magic SysRq

```bash
echo t > /proc/sysrq-trigger 
echo w > /proc/sysrq-trigger
```
