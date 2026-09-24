
# Android System Services Overview

這份筆記整理 Android Framework 層中 **SystemServer、ServiceManager、各系統服務 (System Services)** 的啟動與註冊流程。  
目標是理解 Android 啟動後如何建立 Binder IPC 架構，並讓 App 端能透過 AIDL 存取系統資源。

---

## 1. 系統啟動總覽

Android 開機流程可簡化為以下五階段：

| 階段 | 元件 | 功能 |
| --- | --- | --- |
| **1. Bootloader** | U-Boot / Fastboot | 載入 kernel 與 initramfs |
| **2. Kernel** | Linux Kernel | 初始化硬體與 mount 根檔系統 |
| **3. Init** | `/system/core/init` | 啟動 `zygote`、`servicemanager`、`surfaceflinger` 等核心程序 |
| **4. Zygote** | `app_process` | 啟動 SystemServer（Java Framework 主程序） |
| **5. SystemServer** | `system/framework/services.jar` | 啟動並註冊所有 System Services |

---

## 2. Zygote → SystemServer 啟動流程

Init → zygote → SystemServer → ServiceManager

| 階段 | 關鍵動作 | 主要檔案 |
| --- | --- | --- |
| **Init** | 解析 `init.rc`，啟動 Zygote | `system/core/init/init.cpp` |
| **Zygote** | 建立 Java 執行環境，fork SystemServer | `frameworks/base/core/java/com/android/internal/os/ZygoteInit.java` |
| **SystemServer** | 啟動 Framework 層服務 | `frameworks/base/services/java/com/android/server/SystemServer.java` |

範例（Zygote 內部流程）：
```java
public static void main(String argv[]) {
    registerZygoteSocket();
    preloadClasses();
    if (startSystemServer) {
        forkSystemServer();
    }
}
```

## 3. ServiceManager 與 SystemServiceRegistry

### ServiceManager
-   位於 **native 層**，負責維護所有系統服務的 Binder 註冊表。
-   Java 層的服務會透過 JNI 呼叫到 `ServiceManager.cpp`。
主要函式：

```c++
// frameworks/native/libs/binder/IServiceManager.cpp
sp<IServiceManager> defaultServiceManager() {
    static sp<IServiceManager> gDefault = new BpServiceManager(...);
    return gDefault;
}
```


### SystemServiceRegistry
-   Java 層的登錄機制，負責把服務名稱與對應的 Java 介面綁定。
範例：

```java
registerService(Context.WINDOW_SERVICE, WindowManager.class,
    new CachedServiceFetcher<>() {
        public WindowManager createService(ContextImpl ctx) {
            return new WindowManagerImpl(ctx);
        }
    });
```

## 4. 系統服務註冊與啟動順序

SystemServer 啟動時會依序呼叫 `startBootstrapServices()`、`startCoreServices()`、`startOtherServices()`。

### 啟動流程簡圖
```cscc
SystemServer.main()
  ↓
createSystemContext()
  ↓
startBootstrapServices()
  ↓
startCoreServices()
  ↓
startOtherServices()
  ↓
進入 Looper.loop()
```

### 常見啟動階段對照

| 階段 | 範例服務 | 功能 |
| --- | --- | --- |
| **Bootstrap** | Installer, PowerManager, ActivityManager | 最早啟動，確保系統核心穩定性與基本功能可用。 |
| **Core** | BatteryService, UsageStatsService | Framework 核心服務，負責資源與行為統計。 |
| **Other** | WindowManager, InputManager, AudioService | 與應用層直接互動，提供使用者可見的系統功能。 |


## 5. Binder IPC 在 Framework 層的角色

-   每個系統服務（例如 AMS、WMS、PMS）都是 **Binder 服務端 (Server)**。 
-   App 透過 `Context.getSystemService()` 取得對應的 **Client 代理 (Proxy)**。
-   所有 IPC 呼叫都經過 `/dev/binder` 進行跨程序通信。

```scss
App
  ↓ getSystemService("activity")
SystemServer (ActivityManagerService)
  ↓
Binder 驅動 (binder_ioctl)
  ↓
ServiceManager
```

| 組件 | 角色 | 對應類別 |
| --- | --- | --- |
| **Framework App** | Client | `ActivityManager` (Proxy) |
| **SystemServer** | Server | `ActivityManagerService` |
| **Binder Driver** | IPC 層 | `/dev/binder` |
| **ServiceManager** | 註冊中心 | `IServiceManager` |

**補充說明**

-   `ActivityManager` 是 client 端的代理（Proxy），透過 Binder IPC 呼叫系統服務。
-   `ActivityManagerService` 是 server 端，執行實際的程序與活動管理邏輯。
-   `/dev/binder` 是 kernel 層驅動節點，處理跨進程資料傳遞。
-   `IServiceManager` 是所有服務的中心登錄點，用於查找與註冊 Binder handle。



## 6. 常見系統服務實例

| 服務名稱 | 類別 | 功能摘要 |
| --- | --- | --- |
| **ActivityManagerService (AMS)** | `ActivityManagerService.java` | 管理應用程序的生命週期與進程。 |
| **WindowManagerService (WMS)** | `WindowManagerService.java` | 控制視窗佈局與輸入事件分發。 |
| **PackageManagerService (PMS)** | `PackageManagerService.java` | 管理 APK 安裝、簽章驗證與權限。 |
| **PowerManagerService (PMS)** | `PowerManagerService.java` | 控制電源、螢幕亮度與 suspend/resume 流程。 |
| **AudioService** | `AudioService.java` | 控制音訊路徑、音量以及輸入輸出裝置。 |
| **InputManagerService** | `InputManagerService.java` | 處理觸控、滑鼠與鍵盤輸入事件。 |


## 8. 常見問題與排查

| 問題 | 可能原因 | 修正建議 |
| --- | --- | --- |
| `service list` 中缺少某服務 | 該服務啟動失敗或未註冊 | 檢查 `SystemServer.java` 對應啟動階段的 log。 |
| Binder 連線失敗 (`Transaction failed`) | SystemServer 尚未啟動完成 | 延遲 IPC 呼叫，或確認 `BOOT_COMPLETED` 廣播已發出。 |
| SystemServer crash | 某服務初始化異常 | 透過 `logcat -b system` 找出 `Fatal Exception in SystemServer`。 |
| `SecurityException` | 權限或 SELinux policy 不符 | 檢查 `.te`、`service_contexts`、`AndroidManifest.xml`。 |
| `Permission Denied`（AIDL 呼叫） | service 或 client 權限設定錯誤 | 加上 `android:permission` 屬性或修改 SELinux policy。 |



## 9. 學習與觀察建議
1.  閱讀 `frameworks/base/services/java/com/android/server/SystemServer.java`。
2.  使用 `dumpsys activity services` 檢查服務啟動順序。
3.  嘗試新增一個自訂 SystemService（繼承 `SystemService` 類別）。
4.  觀察 `SystemServiceRegistry.java` 如何登錄服務名稱與介面。
5.  使用 `strace` 或 `perf trace -e binder:*` 觀察 SystemServer 與 Binder 驅動互動。


**延伸閱讀**
-   AOSP: `frameworks/base/services/java/com/android/server/SystemServer.java`   
-   AOSP: `frameworks/native/libs/binder/`  
-   Android Developers – System Services
