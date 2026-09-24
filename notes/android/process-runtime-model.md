# Android Process Runtime Model（Process / Thread / OOM / cgroup 的系統視角）

> 本章定位：
> 
> -   用 **Android System Engineer** 的視角，把「process 從哪來、怎麼被管理、怎麼被殺、資源怎麼被分配」串成一張可 debug 的模型
>     

## 1. 為什麼需要「Process Runtime Model」這一章

可以把 Android 想成：

-   Linux 的 process/thread 當底座
-   上面疊了一個「系統用戶態資源管理層」
    
很多問題（卡頓 / 被殺 / 省電過頭）都不是單點 bug，而是模型的結果：

-   AMS 決定「誰重要」
-   LMKD 決定「誰先死」
-   cgroup 決定「誰拿得到 CPU / memory」
-   scheduler 決定「誰真的跑到 CPU」

沒有橫向模型，你只能在 log 裡盲猜。

## 2. Android Process 從哪裡來：Zygote → app_process

### 2.1 Zygote 的角色（不是普通 daemon）

Zygote 是：

-   預先載入 framework class / resources
-   提供 fork 介面
-   讓新 app 進程快速啟動
    
因此 Android app 的建立不是 `execve()` 主導，而是：

system_server(AMS) → Zygote fork → app process

關鍵含意：

-   大量記憶體是 shared（COW）
-   process startup 的瓶頸往往不是 kernel

### 2.2 spawn 的觀察點

在 debug app 啟動慢時，不要只盯 ActivityThread。

要能回答：

-   fork 有沒有卡？
-   preload 是否過重？
-   是否被 CPU cgroup 壓住？
    
## 3. system_server：Android 資源管理的中樞

system_server 內含：

-   ActivityManagerService（AMS）
-   WindowManagerService（WMS）
-   PackageManagerService（PMS）
-   以及大量 system services
    
在 process runtime 的角度：

-   AMS 負責 process 的「重要性」與生命週期
-   WMS 影響前景互動與可視狀態
    
AMS / WMS 是同一個 runtime 模型的兩側。

## 4. ProcessRecord / UID / Task：Android 用戶態的 process metadata

### 4.1 ProcessRecord（AMS 視角）

AMS 會為每個進程維護 ProcessRecord，包括：

-   uid
-   processName
-   importance
-   oomAdj
    
這些 metadata 會驅動後續行為：

-   cgroup 分配
-   OOM 優先順序

### 4.2 UID 是資源與安全的核心切面

Android 以 UID 為中心來做：

-   權限隔離
-   資源統計
-   cgroup 分組
    
debug 時要習慣用 uid 來追問題。

## 5. OOM 模型：從「重要性」到「可被殺」

### 5.1 OOM Adj 的意義

Android 不直接依賴 Linux OOM killer 的直覺排序。

它在 userspace 自己算出：

-   前景 app
-   service
-   cached
    
再映射成：

-   `oom_score_adj`
    
### 5.2 LMKD（Low Memory Killer Daemon）

現代 Android 多由 LMKD 來主動決策：

-   監測 memory pressure
-   依 oom_score_adj 決定殺誰
    
LMKD 的作用是：
-   在 kernel OOM killer 介入前就收斂
    
## 6. cgroup / scheduler：誰「重要」不代表誰「跑得動」

### 6.1 CPU cgroup：分配時間片的容器

Android 會把不同類型的 thread 放進不同群組，例如：

-   top-app 
-   foreground
-   background
    
這會影響：

-   scheduler 的挑選
-   latency

### 6.2 Thread priority 與 group 的交互

常見誤判：

-   調整 thread priority 就能改善卡頓
    
實務上：

-   priority 只在同 group 內有效
-   group 決定你能不能拿到 CPU
    
debug jank 時，要先看你在什麼 group。

## 7. Debug Toolbox

> 這裡的目標不是列指令，而是提供「最小閉環」： 你能用這些輸出把問題從猜測變成可驗證。

### 7.1 觀察 process / oom

```bash
adb shell ps -A | head

adb shell cat /proc/<pid>/oom_score_adj
```

### 7.2 dumpsys（AMS / memory）

```bash
adb shell dumpsys activity processes

adb shell dumpsys meminfo <package>
```

### 7.3 cgroup 分組

```bash
adb shell cat /proc/<pid>/cgroup

adb shell cat /proc/<pid>/sched
```

### 7.4 觀察 LMKD 行為

```bash
adb logcat | grep -i lmk
```

## 8. 常見問題與排查

### 8.1 常見問題的快速模型定位

#### App 被殺（明明 RAM 看起來還有）

可能原因：

-   memory pressure 來自匿名頁 / file cache 回收困難
-   LMKD 依 adj 決策
    
要查：

-   `dumpsys meminfo`
-   `lmkd` log
-   `oom_score_adj`

#### 前景掉幀 / 觸控卡頓

可能原因：

-   top-app group 沒拿到足夠 CPU
-   binder thread pool 被壓住
    
要查：

-   scheduler latency
-   cgroup 分配

#### 背景服務容易死

可能原因：

-   importance 被降級
-   cached 行為
    
要查：

-   ProcessRecord 的 state
-   oom adj 變化

### 8.2 常見誤判與 Debug

| 現象             | 常見誤判        | 真正原因                             |
|------------------|-----------------|--------------------------------------|
| RAM 還有但被殺   | Android bug     | Memory pressure / LMKD 策略           |
| 系統卡頓         | App 寫得爛      | Top-app cgroup / Scheduler latency    |
| 背景服務常被殺   | Service bug     | Importance / OOM adj 等級下降         |
