# Linux Tracing Playbook

> 目的：把 tracing 從「工具使用」提升為**可複製的 debug 流程**。

## 1. Tracing 的定位

Tracing 適用於：

-   問題與「時間順序」有關
-   問題不是必現、或無法靠 log 重現
-   需要確認「誰先卡住、誰在等誰」    

**不適合直接用 tracing 的情境**：

-   明確的功能錯誤（先用 log / dynamic debug） 
-   單點 return code 錯誤

## 2. Tracing 層級與工具選擇

| 層級         | 典型問題                     | 建議工具                    |
|--------------|------------------------------|-----------------------------|
| Scheduler    | 卡頓、延遲、抖動             | sched trace / perf          |
| IRQ          | 中斷延遲、中斷遺失           | irq trace                   |
| Power        | suspend / resume 失敗        | power trace                 |
| Driver       | call flow 不明               | function_graph              |
| System-wide  | 不明原因 hang                | Magic SysRq + ftrace        |

**原則**：

> 永遠先用「事件 trace」，最後才用 function trace。

## 3. 基礎環境

```bash
mount -t tracefs nodev /sys/kernel/tracing

cd /sys/kernel/tracing
```
**基本檢查**：
```bash
cat available_tracers

cat available_events | head
```

## 4. 事件型 Tracing

### 4.1 Scheduler

適用：

-   卡頓
-   suspend/resume 停在某 task

```bash
echo 0 > tracing_on

echo sched_switch > set_event

echo 1 > tracing_on
```
**判讀重點**：

-   是否有 task 長時間未被排程   
-   是否被低優先權 task 壓住

### 4.2 IRQ

適用：

-   driver 沒反應  
-   硬體看似正常但軟體沒收到事件

```bash
echo irq:irq_handler_entry > set_event

echo irq:irq_handler_exit >> set_event
```
**判斷方向**：

-   IRQ 有沒有進來
-   handler 是否過久

### 4.3 Power（Suspend / Resume）

```bash
echo power:suspend_resume > set_event

echo power:device_pm_callback_start >> set_event

echo power:device_pm_callback_end >> set_event
```
**常見卡點**：

-   device callback 未 return
-   ordering 問題（consumer 先於 supplier）

## 5. Function Tracing

### 5.1 function_graph

適用：

-   driver call flow 不確定
-   懷疑卡在某層 abstraction

```bash
echo function_graph > current_tracer

echo foo_driver_* > set_ftrace_filter
```
**一定要做的事**：

-   限定 filter 
-   限定觀測時間 

## 6. Trigger / Filter

### 6.1 Event Filter

```bash
echo 'comm == "surfaceflinger"' > events/sched/sched_switch/filter
```

### 6.2 Trigger

```bash
echo 'traceon' > events/power/suspend_resume/trigger
```

## 7. 與 perf 的分工

| 問題         | 使用工具 |
|--------------|----------|
| 哪裡最熱     | perf     |
| 為什麼卡住   | ftrace   |

```bash
perf record -g -- sleep 10

perf report
```

## 8. 實戰案例

### 8.1 Case 1：Suspend 停在黑畫面

**流程**：

1.  power trace 找最後一個 callback
2.  sched trace 看是否 blocked
3.  function_graph 只 trace 該 driver

### 8.2 Case 2：Poweroff 變 reboot

**流程**：

1.  pstore 確認是不是 panic
2.  power trace 確認 shutdown path  
3.  function trace 對 pm_power_off

### 8.3 Case 3：音訊播放無聲

**流程**：

1.  IRQ trace 確認 DMA IRQ
2.  sched trace 看 audio thread  
3.  function_graph trace dai trigger

## 9. 常見問題與排查（常見陷阱）

-   trace 開太久 → buffer 爆   
-   function trace 不設 filter
-   trace 本身改變 timing
-   沒留 vmlinux

## 10. 決策總結

問題是否與時間順序有關？
```yaml
└─ 否 → 不要 trace

└─ 是 → 事件 trace

└─ 不夠 → function trace
```

## 11. 敘事

> 「會先用 sched / power event trace 確認卡點， 只有在事件不足以定位時， 才用 function_graph 限縮 trace driver call flow。」
