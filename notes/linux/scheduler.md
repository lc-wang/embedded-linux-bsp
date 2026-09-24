# Linux Scheduler（排程器）解析：CFS / RT / Deadline / RQ / Load Balance

本章介紹 Linux 行程排程器的整體架構及運作機制，包含：

- Runqueue（rq）
- CFS（Completely Fair Scheduler）
- 負載平衡（load balancing）
- 調度類型：CFS、RT、Deadline
- CFS VRuntime 機制
- Context Switch
- Affinity / cgroup / sched policy
- Debug 與效能問題排查方法

適用於 BSP、效能調校、低延遲應用、Android SoC 調校。

## 1. Scheduler 總覽

Linux 主要使用 **multi-class scheduler**：

| 類型 | 說明 |
| --- | --- |
| **CFS** | 一般任務（default） |
| **RT（real-time）** | 高優先權，FIFO / RR |
| **Deadline** | 嚴格時間限制的排程（需特殊系統） |
| **Idle** | 無工作時的 fallback |

Linux 以 CFS 為核心，每個 CPU 有獨立的 runqueue。

排程流程：
```text
task wakeup
↓
enqueue_task()
↓
scheduler tick (or wakeup preemption)
↓
pick_next_task()
↓
context switch
```

## 2. Runqueue（RQ）

每個 CPU 有一個 `struct rq`：
```c
struct rq {
struct cfs_rq cfs;
struct rt_rq rt;
struct dl_rq dl;
...
struct task_struct *curr;
}
```

RQ 內容：

- 正在執行的 task（curr）
- 可執行的 CFS task（紅黑樹）
- RT task queue
- Deadline task queue
- load / utilization 計算
- CPU usage 統計

## 3. CFS（Completely Fair Scheduler）

CFS 的核心理念：

每個 task 取得公平的 CPU 時間。

公平的衡量方式：**vruntime（虛擬執行時間）**

### 3.1 vruntime（最重要概念）

執行越久 → vruntime 增加越多
nice 值越低（優先權高）→ vruntime 增加越慢

CFS 透過維護一棵 **紅黑樹（rb-tree）** 來管理所有 runnable tasks：

最左邊節點 = vruntime 最小 = 應優先執行

執行步驟：
```text
pick_next_task()
← 取 rb-tree 最左節點
```
Task 結束執行後會更新 vruntime：
```text
vruntime += actual_runtime * weight_factor
```

## 4. 調度類型 (Sched Class)

Linux 具有三大排程類別：

| 類別 | 說明 | 資料結構 |
| --- | --- | --- |
| **SCHED_NORMAL / CFS** | 大部分工作 | cfs_rq |
| **SCHED_FIFO / SCHED_RR** | Real-time | rt_rq |
| **SCHED_DEADLINE** | 嚴格 deadline | dl_rq |

優先權順序：
```text
deadline > RT > CFS
```

## 5. Wakeup Preemption（喚醒搶佔）

新任務 woken up 時，有機會搶佔目前執行的 task。

邏輯：
```text
if new->vruntime < curr->vruntime - margin
preempt
```
讓 wakeup 更 reactive（改善系統互動性）。

## 6. Load Balancing（多核心負載分散）

適用於 multi-core SoC（你常用的 RK3588 / RZ/T2H / i.MX8）。

Linux scheduler 會：

- 平衡 CPU 間可執行任務數目
- 避免某 core 過載
- 將 tasks 移動到較空閒的 core（migration）

Load balance 會在：

- scheduler tick
- idle CPU
- 定期（interval）

執行。

## 7. Affinity / cgroup CPU 限制

### 7.1 CPU affinity

強制任務只能在特定 CPU 跑：
```bash
taskset -c 1,2 ./myapp
```

### 7.2 cgroup 限制 CPU 使用率

```bash
systemd-run --scope -p CPUQuota=50% ./app
```
Android 中：
- foreground / background 也透過 cgroup 調度

## 8. Context Switch

Context switch 發生於：

- 另一個 task 擁有更高優先權
- 時間配額到期
- I/O block / unblock
- preemption

Context switch 成本包括：

- CPU pipeline flush
- TLB reload
- cache pollution

因此太高的 switch rate → 效能下降。

## 9. Scheduler 與 CPUFreq / Thermal 的關係

Scheduler 會與 CPUFreq / thermal 整合：
```text
schedutil governor:
Q = estimated cpu utilization
→ 選擇適合頻率
```
Thermal throttling:
```text
thermal zone → cooling device → 降低 CPU OPP（頻率）
scheduler → 使用較低 CPU capacity
```
Android 特別依賴 scheduler + cpufreq + thermal 的互動。

## 10. 常見 Debug 工具

### 10.1 查看任務排程狀態

```bash
ps -eo pid,cls,pri,rtprio,ni,stat,comm
```

### 10.2 追蹤 scheduler events（強力）

```bash
trace-cmd record -e sched_switch -e sched_wakeup
trace-cmd report
```

### 10.3 檢查 CPU 利用率

```bash
top
htop
perf top
```

### 10.4 檢查 runqueue 狀況

```bash
cat /proc/sched_debug
```

### 10.5 ftrace（追蹤排程延遲）

```bash
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable
cat /sys/kernel/debug/tracing/trace
```

## 11. 常見問題與排查

| 問題 | 可能原因 | 解決方式 |
| --- | --- | --- |
| 某 CPU load 過高 | task pinned / affinity 限制 | 檢查 taskset / cgroup |
| UI 卡頓（Android） | foreground task 被 background 搶 CPU | 調整 cgroup / uclamp |
| 頻率低、效能差 | schedutil + thermal throttle | 查看 thermal zone log |
| Latency 高 | RT task 搶占 CFS | 檢查 SCHED_FIFO 程序 |
| 多核效能未跑滿 | load balance 不佳 | CPU topology / sched_domain 問題 |
