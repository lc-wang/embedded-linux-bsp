# Android 開機最佳化（Boot Optimization & SystemServer Performance）

## 1. Android 開機流程總覽

```yaml
Bootloader
↓
Linux Kernel
↓
init (first user-space process)
↓
Zygote 進程
↓
SystemServer
↓
Launcher/App Home
```

## 2. Init（rc 腳本）最佳化

### 2.1 init.rc / vendor.rc 優化方向

| 問題 | 原因 | 優化方向 |
| --- | --- | --- |
| 開機卡很久 | rc 中 service 太多 | 延後啟動 non-critical services |
| IO 過高 | 同時啟動大量 service | 使用 `class_start` 分組啟動 |
| SELinux 遲滯 | policy 過大 | 減少無用 domain / type |
| mount 很慢 | 檔案系統初始化過慢 | 改用 ext4 + journal mode 優化 |

#### 建議技巧

- 使用 `on property:sys.boot_completed=1` 啟動次要服務  
- 將非必要 daemon 移到 late_start class  
- 避免 rc 中大量 `exec`（阻塞）

## 3. Zygote 啟動與 preload 優化

### 3.1 Zygote preload

Zygote 在啟動時會預載：

- class preload (`framework-preloaded-classes`)
- native preload
- resource preload

#### 優化方向

| 問題 | 原因 | 優化 |
| --- | --- | --- |
| Zygote 花太久 | preload class 太多 | 減少沒使用的 preloaded class |
| fork 耗時長 | ART daemon 未啟動 | 確保 `zygote64` / `zygote_secondary` 正常執行 |

## 4. SystemServer 啟動時間分析

### 4.1 SystemServer 啟動流程

```yaml
StartBootstrapServices()
StartCoreServices()
StartOtherServices()
```

#### 啟動階段分類

| 階段 | 內容 |
| --- | --- |
| Bootstrap | PowerManager, AMS, Installer |
| Core | BatteryService, UsageStats |
| Other | WMS, Input, Audio, Media |

### 4.2 SystemServer 延遲分析

#### 延遲來源

| 型態 | 說明 |
| --- | --- |
| I/O 拖慢 | service 在 init 過程中存取大量檔案 |
| binder 阻塞 | 某服務等待其他服務 ready |
| native service loading | HAL 透過 HIDL/AIDL 初始化過久 |
| 記憶體不足 | low-memory + zram 導致 GC 過多 |

#### 重要 log

logcat -b system
dmesg | grep init
dumpsys activity service SystemServerTiming

yaml
複製程式碼

## 5. Dexopt / ART 優化

### 5.1 Dexopt 影響開機速度

Dexopt 的影響主要在首次開機，但多數專案也會調整為：

- defer dexopt
- background dexopt
- preopt at build time

#### 優化方向

| 技術 | 說明 |
| --- | --- |
| **dex2oat 預編譯** | A/B OTA 會大量依賴 preopt |
| **profile guided optimization (PGO)** | 常用方法，用以提升 app 啟動速度 |
| **app image** | preloading class metadata |

## 6. BootAnimation timing / graphics pipeline

### 6.1 bootanimation 相關時序

#### 啟動流程

```yaml
SurfaceFlinger start
↓
HWC、Display 初始化
↓
bootanimation 啟動（開機畫面）
```

#### 可能 bottleneck

- DRM / HWC 初始化太慢  
- gralloc allocator 等待 ion / dmabuf  
- SurfaceFlinger 尚未 ready  

檢查：
```yaml
dumpsys SurfaceFlinger --latency
```

## 7. FileSystem / IO 改善

### 7.1 filesystem 對開機速度的影響

| 問題 | 來源 | 解法 |
| --- | --- | --- |
| FS 掛載慢 | ext4 with journal full | 調整 journal size / mode |
| zram 低效 | 內存低 | 設定壓縮模式 lz4 |
| 檔案 IO 太多 | 太多 init 服務讀檔 | 分批啟動 |

### 7.2 I/O Prefetch（Android Go 的技巧）

Android Go（低階裝置）使用 **metadata prefetch** 加速啟動：

- 在 system.img 生成時壓縮 metadata
- 開機時一次讀進記憶體

這對 NOR / SD 卡裝置特別有效。

## 8. Debug 與 Profiling（開機卡住怎麼找原因）

### 8.1 Init 卡住 Debug

```yaml
dmesg -n 8
dmesg | grep init
logcat -b all
```

若 rc 無輸出 → bootloader / kernel early log 未開  
→ 使用 `earlycon`。

### 8.2 SystemServer 卡住

```yaml
logcat -b system
logcat | grep SystemServerTiming
```
或使用：
```yaml
dumpsys activity service SystemServerTiming
```

### 8.3 Zygote 問題

常見問題：

| 問題 | 原因 | 解法 |
| --- | --- | --- |
| Zygote hang | preload 卡住 | 移除 preload class |
| fork timeout | 創建 app process 太慢 | 調整 low-memory + native preload |
| SELinux 拒絕 | policy 問題 | 檢查 audit log |

## 9. 常見問題與排查

### 9.1 啟動常見問題表

| 問題 | 可能原因 | 處理方式 |
| --- | --- | --- |
| 開機非常慢 | IO／dexopt／SystemServer 問題 | 逐層分析 log |
| 卡在開機動畫 | SF/HWC 初始化過慢 | 查 SurfaceFlinger log |
| 卡在 SystemServer | 某服務初始化失敗 | logcat -b system |
| 卡在 Zygote | preload class 太多 | 減少 preload |
| 完全沒 log | kernel log 被關、earlycon 未開 | 開啟 early printk |
