# Android 開機流程總覽 (Boot Flow)

本章說明 Android 從上電後至 Framework 啟動的整個流程，  
涵蓋 **Bootloader → Kernel → Init → Zygote → SystemServer → App** 的階段。  
理解這條鏈是分析啟動錯誤與系統 bring-up 的基礎。

## 1. 開機流程總覽

```text
[Boot ROM]
↓
[Bootloader (SPL → U-Boot)]
↓
[Kernel]
↓
[Init]
↓
[Zygote]
↓
[SystemServer]
↓
[App Process]
```

| 階段 | 元件 | 功能 |
| --- | --- | --- |
| Boot ROM | SoC 內部程式 | 從儲存裝置載入 Bootloader |
| Bootloader | U-Boot / Fastboot | 初始化 DDR / PMIC，載入 Kernel |
| Kernel | Linux 核心 | 初始化驅動、mount 檔案系統 |
| Init | `/system/core/init` | 啟動 system 進程與 zygote |
| Zygote | `app_process` | 建立 Java 執行環境，fork SystemServer |
| SystemServer | `services.jar` | 啟動所有系統服務 |
| App Process | Android 應用層 | 與 Framework IPC 互動 |

## 2. Bootloader 階段

Bootloader 通常分為兩階段：  
**SPL (Secondary Program Loader)** 與 **U-Boot Proper**。

| 階段 | 主要任務 | 對應檔案 |
| --- | --- | --- |
| SPL | 初始化記憶體、PMIC、UART | `spl/` |
| U-Boot Proper | 載入 Kernel / DTB / ramdisk | `u-boot/` |

流程：
Power On
→ SoC Boot ROM 載入 SPL
→ 初始化 DDR / PMIC
→ 載入 U-Boot Proper
→ 從指定儲存裝置 (eMMC / SD / SPI) 讀取 Kernel
→ 跳轉至 Kernel Entry

Bootloader 常見設定檔：
- `bootargs`：傳遞給 kernel 的啟動參數  
- `fdtaddr`：DTB 地址  
- `initrd`：initramfs 所在位置  

## 3. Kernel 階段

Kernel 啟動後的主要工作如下：

| 階段 | 動作 |
| --- | --- |
| 設定 CPU / MMU / cache | 初始化硬體環境 |
| 掛載 initramfs | 提供初始使用者空間 |
| 啟動第一個使用者空間程式 | `/init` |
| 載入驅動模組 | platform driver、binder、fbdev、音訊等 |
| 輸出 log | 可從 `dmesg` 觀察 |

關鍵 log 標誌：
```text
[ 0.000000] Booting Linux on physical CPU
[ 0.123456] Run /init as init process
```

## 4. Init 階段與 rc system

Init 是 Android user-space 的第一個進程（PID 1）。  
它負責解析 `.rc` 腳本並啟動系統關鍵服務。

| 檔案 | 功能 |
| --- | --- |
| `/init.rc` | 主配置檔，載入其他子配置 |
| `/init.<hardware>.rc` | 平台專屬設定 |
| `/vendor/etc/init/*.rc` | vendor 分區服務 |
| `/system/core/init/` | 原始碼位置 |

### 4.1 範例流程

```text
/init.rc
├─ mount /dev /proc /sys
├─ start servicemanager
├─ start surfaceflinger
├─ start zygote
└─ start logd
```

### 4.2 常見指令

- `service <name> <path>`：定義服務啟動命令  
- `on boot`：開機觸發事件  
- `setprop`：設定屬性值  

## 5. Zygote 啟動流程

Zygote 是整個 Android Java 世界的根，  
負責建立虛擬機 (ART)、預載 Framework 類別並 fork 出 SystemServer。

| 階段 | 關鍵檔案 | 功能 |
| --- | --- | --- |
| 啟動指令 | `init.rc` (`start zygote`) | 由 init 啟動 |
| 程式 | `/system/bin/app_process` | 執行 Java 入口點 |
| 主類別 | `ZygoteInit.java` | 建立 socket，等待 fork 請求 |

流程：
```text
Init 啟動 Zygote → 建立 socket → 載入 framework 類別
↓
等待 AMS 請求 → fork() → 新的 app process
```

核心程式段：
```java
public static void main(String argv[]) {
    registerZygoteSocket();
    preloadClasses();
    if (startSystemServer) {
        forkSystemServer();
    }
    runSelectLoop();
}
```

## 6. SystemServer 啟動流程

Zygote fork 出的 SystemServer 負責啟動整個 Framework 層服務。
它位於 `frameworks/base/services/java/com/android/server/SystemServer.java。`

流程概覽：

```text
Zygote → SystemServer.main()
     ↓
createSystemContext()
     ↓
startBootstrapServices()
startCoreServices()
startOtherServices()
     ↓
Looper.loop()
```

| 啟動階段 | 主要服務 | 功能 |
| --- | --- | --- |
| **Bootstrap** | AMS, PMS, PowerManager | 啟動基礎系統服務，建立 Framework 核心骨架。 |
| **Core** | BatteryService, UsageStatsService | 啟動核心功能服務，負責資源與系統狀態統計。 |
| **Other** | WMS, InputManager, AudioService | 啟動使用者層服務，處理視窗、輸入與音訊。 |

完成後 Framework 即可接受 App IPC 呼叫。

**補充說明**

-   **Bootstrap 階段**：啟動 ActivityManagerService (AMS)、PackageManagerService (PMS)、PowerManagerService 等最關鍵元件。
-   **Core 階段**：確保系統可正常管理電池、使用統計與性能監控。
-   **Other 階段**：啟動 UI、音訊、輸入等與使用者互動的服務。

## 7. App 啟動與 Binder 連線

App 啟動由 AMS 發起：

```text
Launcher → AMS.startActivity()
   ↓
ActivityThread.main()
   ↓
attachApplication()
   ↓
建立 Binder IPC 與 SystemServer 連線
```

| 組件 | 角色 | 主要功能 |
| --- | --- | --- |
| **ActivityManagerService (AMS)** | Server | 控制應用啟動、切換與生命週期管理。 |
| **ActivityThread** | Client | 應用主執行緒（Main Looper），負責事件循環與 UI 更新。 |
| **ApplicationThread** | Binder Proxy | 負責與 SystemServer 進行 IPC，接收來自 AMS 的指令。 |
| **Instrumentation** | 控制生命週期 | 呼叫應用的 `onCreate()`、`onResume()` 等回調函式。 |

**補充說明**
-   `ActivityManagerService` 為整個應用管理中心，位於 SystemServer 內。

-   `ActivityThread` 是每個應用程序的主執行緒（對應 UI thread）。

-   `ApplicationThread` 實際是 Binder 端點，負責 IPC 溝通。

-   `Instrumentation` 用於控制應用啟動與測試（例如單元測試框架會覆蓋它）。

## 8. Debug 與分析方法

| 工具 / 節點 | 功能 |
| --- | --- |
| `dmesg` | 查看 Kernel 啟動訊息，確認驅動與掛載過程。 |
| `logcat -b all` | 查看 Init、Zygote、SystemServer 的完整 log 輸出。 |
| `/proc/1/maps` | 驗證 Init 是否載入正確的 `.rc` 腳本與庫。 |
| `ps -A \| grep zygote` | 檢查 Zygote 是否啟動與進程 ID。 |
| `service list` | 檢查 SystemServer 服務註冊狀態。 |
| `systrace` / `perf trace` | 追蹤開機過程中的性能瓶頸與系統事件時序。 |

## 9. 常見問題與排查

| 問題 | 可能原因 | 解決方式 |
| --- | --- | --- |
| **系統卡在 Kernel Logo** | DTB 錯誤或 rootfs 無法掛載 | 檢查 `bootargs` 與 partition layout。 |
| **卡在 Init 階段** | `.rc` 腳本錯誤或檔案遺失 | 透過 UART 或 `logd` 檢查輸出。 |
| **無法啟動 Zygote** | `/system/bin/app_process` 缺失或權限錯誤 | 檢查 system image 完整性與檔案權限。 |
| **SystemServer crash** | 某服務初始化失敗 | 使用 `logcat -b system` 尋找 `Fatal Exception in SystemServer`。 |

**補充檢查建議**

```bash
# 查看 kernel 啟動 log
dmesg | grep -i error

# 檢查 Zygote 是否啟動
ps -A | grep zygote

# 確認 binder 裝置存在
ls -l /dev/binder

# 查看 SystemServer 異常
logcat -b system | grep SystemServer

```

## 附錄

### A. 延伸閱讀

-   `system/core/init/`
-   `frameworks/base/core/java/com/android/internal/os/ZygoteInit.java`
`frameworks/base/services/java/com/android/server/SystemServer.java` 
-   Android Boot Sequence Overview – source.android.com
