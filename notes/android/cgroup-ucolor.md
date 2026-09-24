# Android cgroup 與 uclamp（ucolor）實務筆記

> 本章目標：
> 
> -   以 **Android System / Kernel Engineer** 視角，理解 Android 如何透過 **cgroup + uclamp** 影響 Linux scheduler
>     
> -   能實際 **debug 效能 / 卡頓 / 功耗異常**
>     
> -   能回頭修改 **kernel / system_server / init rc** 來驗證行為
>     

## 1. 為什麼 Android 需要 cgroup + uclamp（而不只是 Linux scheduler）

在純 Linux 環境中：

-   scheduler 的目標是 **公平性（fairness）與整體吞吐**
-   預設假設：
    -   workload 長時間存在
    -   使用者不在乎「瞬間互動延遲」

但 Android 的特性完全不同：

| Android 特性                         | 對 Scheduler 的衝擊                           |
|-------------------------------------|----------------------------------------------|
| 互動導向（UI / Touch / Animation）  | 需要在短時間內瞬間拉高 CPU 計算能力           |
| 前景 / 背景 App 切換頻繁            | Task importance 會高度動態變化                |
| SoC 功耗直接影響 UX                 | Scheduler 不能只追求效能，需平衡功耗與體驗     不能只追求效能

**Android 不能只靠 scheduler heuristic**  
必須有 **系統層主動介入 scheduler 決策**

這正是：

-   cgroup（分類與資源隔離）
-   uclamp（限制 scheduler 的效能選擇範圍）

存在的核心理由。

## 2. Android 使用的 cgroup 架構

### 2.1 Android 使用的是 cgroup v2

Android 11 之後：
-   **統一使用 cgroup v2**（single hierarchy）
-   scheduler、cpuset、cpu、memory 整合

但：

-   設計思想仍延續自早期 Android cgroup v1
-   system_server / init / lmkd 邏輯仍帶有「角色導向」

### 2.2 Android 的核心 cgroup 分類邏輯

Android **不是用 PID 直接管理**，而是：

> 「以 _process role_ 為中心」

典型分類（簡化）：

| cgroup           | 用途說明                               |
|------------------|----------------------------------------|
| top-app          | 前景互動中的 App（UI / Touch / Animation） |
| foreground       | 可見但非互動的 App                     |
| background       | 背景執行的 App                         |
| system           | system_server 與核心 Native Services   |
| camera-daemon    | Camera pipeline 相關服務與 daemon      |

這些不是 kernel 定義，而是 **Android framework 定義 → 寫入 cgroup fs**。

## 3. uclamp 是什麼？為什麼 Android 要它

### 3.1 scheduler 的核心問題

Linux scheduler 在 EAS（Energy Aware Scheduling）下：

-   會根據 task util（util_avg）選 CPU
-   但 **util 是歷史平均值**

結果：
-   UI thread 剛醒來時 util 很低
-   scheduler 可能選到小 core
-   → jank

### 3.2 uclamp 的本質

uclamp = **utilization clamp**

它做的事情非常單純但關鍵：

> 為 task / cgroup 設定
> 
> -   util_min
>     
> -   util_max
>     

限制 scheduler 計算出來的 util 範圍。

```text
final_util = clamp(util_avg, util_min, util_max)
```

### 3.3 Android 為什麼大量用「cgroup uclamp」

Android 幾乎 **不對單一 task 手動設 uclamp**，而是：

-   以 cgroup 為單位 
-   framework 控制「角色 → uclamp policy」

原因：

-   process / thread 太多
-   task 生命週期短
-   role 比 task 穩定

## 4. Android 的 uclamp policy 實際長怎樣

### 4.1 常見設定範例（概念）

```text
/top-app:
  uclamp.min = 512
  uclamp.max = 1024

/foreground:
  uclamp.min = 128

/background:
  uclamp.max = 256
```

語意：

-   top-app：
    -   **保證至少中高效能 core**
-   background：
    -   **禁止搶大 core**

### 4.2 這不是 magic，是明確 trade-off

| 設定過高的風險        | 設定過低的風險        |
|---------------------|---------------------|
| 功耗暴增             | UI jank             |
| 觸發 thermal throttle| Latency 抖動         |

Android tuning 的本質：

> 「在可感知延遲_ 與 _功耗_ 間找甜蜜點」

## 5. Android framework → kernel 的控制路徑

### 5.1 誰在寫 cgroup / uclamp？

實際路徑：

```text
ActivityManager / WindowManager
        ↓
 system_server
        ↓
 libprocessgroup
        ↓
 /sys/fs/cgroup/...
```

重點：

-   **kernel 完全不知道什麼是 top-app**
-   kernel 只看到：
    -   某個 cgroup 設了 uclamp

### 5.2 debug 重點

當效能異常時，你要問的是：

1.  這個 thread 現在在哪個 cgroup？
2.  該 cgroup 的 uclamp 設定是什麼？
3.  scheduler 是否真的照這個值在跑？

## 6. 實戰 Debug：UI 卡頓怎麼查

### 6.1 先從 userspace 確認角色

```bash
ps -e -o pid,comm,cgroup | grep surfaceflinger
```

### 6.2 檢查 cgroup uclamp

```bash
cat /sys/fs/cgroup/top-app/uclamp.min
cat /sys/fs/cgroup/top-app/uclamp.max
```

### 6.3 trace scheduler 決策

```bash
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_switch/enable
echo 1 > /sys/kernel/debug/tracing/events/sched/sched_wakeup/enable
```

觀察：

-   wakeup → runqueue
-   CPU 類型是否符合預期

## 7. 從 BSP / kernel 角度常見踩雷點

### 7.1 kernel config 沒開 uclamp

```text
CONFIG_UCLAMP_TASK=y
CONFIG_FAIR_GROUP_SCHED=y
```

少一個：

-   framework 設了
-   kernel 直接忽略

### 7.2 cpufreq / EAS 與 uclamp 不一致

-   uclamp 只影響 util
-   cpufreq governor 邏輯錯 → 還是慢

**uclamp 不是萬能**

### 7.3 vendor kernel 魔改 scheduler

常見問題：

-   vendor patch 覆蓋 uclamp
-   debug 時看到值正確但行為不對

## 8. 與其他 subsystem 的關聯

### 8.1 與 Binder

-   binder thread pool 可能被放錯 cgroup
-   造成 system_server latency

### 8.2 與 LMKD

-   記憶體壓力 → 調整 cgroup priority
-   間接影響 CPU

### 8.3 與 thermal

-   thermal throttle 會壓低 freq
-   即使 uclamp 高也無法突破

## 9. 本章應該真正記住的事情

1.  **Android 的效能不是 scheduler 自己決定的**
2.  cgroup 是角色建模工具，不是單純資源限制
3.  uclamp 是 Android 控制 UX latency 的核心武器
4.  debug 必須跨：
    -   framework
    -   cgroup fs
    -   scheduler trace
