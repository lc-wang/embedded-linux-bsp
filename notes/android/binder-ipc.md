# Android Binder IPC 機制

這份筆記說明 Android IPC（Inter-Process Communication）的核心機制 —— **Binder**。  
它是 Android Framework、System Server、HAL 與應用程式之間的主要通訊管道。

## 1. Binder 架構概覽

Binder 是一種以 **客戶端 / 伺服端模型 (Client-Server)** 為基礎的 IPC 機制。  
它運行於 Kernel space（binder driver）與 User space（libbinder / Java Binder）之間。

Client (App)
↓ ↑
libbinder
↓ ↑
Binder Driver (/dev/binder)
↓ ↑
libbinder / Java Binder
↓ ↑
Server (Service)

| 區層 | 元件 | 功能 |
| --- | --- | --- |
| **User Space - Framework** | Java Binder / AIDL | 提供高層抽象（`IBinder`、`ServiceManager`） |
| **User Space - Native** | libbinder.so | C++ API 封裝、序列化與交易發送 |
| **Kernel Space** | binder.ko | Binder 核心驅動，負責 IPC 傳輸、引用計數與 thread 管理 |

## 2. Binder 核心元件

| 元件 | 位置 | 說明 |
| --- | --- | --- |
| **/dev/binder** | Kernel | Binder driver 裝置節點 |
| **binderfs** | Kernel | 新版 Binder 掛載點，通常位於 `/dev/binderfs/` |
| **ServiceManager** | User space | 管理所有已註冊的系統服務 |
| **BpBinder / BBinder** | libbinder | Binder 的 client/server 代理類別 |
| **ProcessState / IPCThreadState** | libbinder | 管理 thread 與 Binder driver 溝通 |
| **BinderTransactionData** | Kernel 結構 | 描述單次交易的內容與目標 |
| **flat_binder_object** | Kernel 結構 | 表示一個 binder handler 或 file descriptor |

## 3. Binder Driver 核心資料流

Client Process
  ↓ binder_write_read (ioctl)
Binder Driver
  ↓
解析 binder_transaction_data
  ↓
找到對應的 target thread
  ↓
將資料送入目標 thread 的 todo queue
  ↓
喚醒對方 thread 處理 transaction
  ↓
完成後回傳 binder_reply 給 client

### 3.1 核心結構體

| 結構體 | 主要用途 |
| --- | --- |
| `struct binder_proc` | 每個 process 的 Binder 狀態（管理 memory map、引用計數）。 |
| `struct binder_thread` | 管理 thread 資訊與待處理 transaction。 |
| `struct binder_transaction` | 表示一次完整的 IPC 傳輸。 |
| `binder_node` / `binder_ref` | 用於跨程序的物件引用與生命週期管理。 |

## 4. 服務註冊與查找流程

System Server 啟動 → 建立 ServiceManager
  ↓
每個服務（如 ActivityManagerService）註冊自己：
  addService("activity", binder)
  ↓
Client 透過 getService("activity") 查詢 binder 引用
  ↓
ServiceManager 回傳目標 binder 的 handle
  ↓
Client 持有 proxy (BpBinder)，透過 ioctl 與 Server 通訊

### 4.1 操作與對應函式

| 操作 | 主要函式 | 備註 |
| --- | --- | --- |
| **註冊服務** | `addService()` → `BpBinder::transact()` | Server 端註冊系統服務至 ServiceManager |
| **查詢服務** | `getService()` → `binder_ioctl()` | Client 端向 ServiceManager 查詢服務 |
| **IPC 呼叫** | `transact(code, data, reply)` | 以 transaction 為單位的 IPC 傳輸 |
| **回應處理** | `onTransact()` / `reply.read*()` | Server 端回傳結果給 Client |

**補充說明**
- `ServiceManager` 是所有 Binder 服務的中央登錄機制。  
- Client 與 Server 之間不會直接連線，而是透過 Binder driver 與 ServiceManager 轉譯。  
- 你可以透過以下指令觀察當前註冊的服務：
```bash
service list
dumpsys -l
```

## 5. Binder 通訊方向與記憶體映射

Binder 透過共享記憶體 (mmap)與 `copy_from_user` / `copy_to_user`
進行資料傳輸。  
以下是整個傳輸路徑概覽:

Client → ioctl(BINDER_WRITE_READ)
↓
Binder Driver 收到請求，建立 binder_transaction
↓
copy_from_user() 將資料從使用者空間複製到驅動暫存區
↓
驅動尋找目標 thread，將 transaction 放入其 queue
↓
Server thread 被喚醒，binder_read() 取出資料並反序列化
↓
Server 處理後以 binder_reply 回傳結果給 Client

### 5.1 傳輸階段對照表

| 階段 | 動作 | 備註 |
| --- | --- | --- |
| **`ioctl(BINDER_WRITE_READ)`** | App 發出 Binder 請求 | Binder 驅動進入 transaction |
| **驅動層拷貝資料** | `copy_from_user()` → 暫存區 | 將用戶空間資料複製到 kernel |
| **傳遞至目標執行緒** | 驅動將資料放入 server thread queue | Transaction 等待處理 |
| **Server 收到資料** | `binder_read()` 取出、反序列化 | 交由對應的 onTransact() 處理 |
| **回傳結果** | `binder_reply()` → Client | 將處理結果返回給請求端 |

**補充說明**
- 每個進程在第一次使用 Binder 時，會 mmap 一段共享記憶體（通常 1MB）。  
- Binder Driver 在不同進程間進行 **零拷貝共享傳輸**，只在必要時使用 `copy_from_user` / `copy_to_user`。  
- 你可以透過以下方式觀察 mmap：
```bash
cat /proc/<pid>/maps | grep binder
```

## 6. AIDL / HIDL 與 Native Binder

Android Binder IPC 在不同層級有多種實作方式。  
它們的共通點是都透過 **Binder driver** 傳遞資料，  
差異在於語言層級、生成工具與使用場景。

### 6.1 Binder IPC 類型比較

| 類型 | 使用層級 | 範例 | 特點 |
| --- | --- | --- | --- |
| **AIDL (Java)** | Framework 層 (App / System Server) | `IAudioService.aidl` | 自動產生 Proxy / Stub，最常用於 Framework。 |
| **HIDL** | HAL 層 (Android 8 ~ 11) | `android.hardware.camera@2.4` | C++ 接口，舊版 HAL IPC 架構。 |
| **AIDL (stable)** | HAL 層 (Android 12+) | `aidl_interface` in Soong | 新版取代 HIDL，支援版本穩定性與 backward 兼容。 |
| **Native Binder (C++)** | System / Daemon 層 | `BpBinder`, `BBinder` | 直接使用 libbinder API 實作 client / server。 |

### 6.2 AIDL 範例

```c
IHelloService.aidl
package com.example.hello;

interface IHelloService {
    void sayHello(String name);
}
```

生成後自動建立三個類別：

| 類別 | 功能 |
| --- | --- |
| `IHelloService` | 介面定義，描述可呼叫的方法。 |
| `Stub` | Server 端實作，繼承自 `Binder`，負責接收與處理 transaction。 |
| `Proxy` | Client 端代理，透過 `transact()` 將資料傳給 Server。 |

### 6.3 Native Binder (C++) 範例

```c
server.cpp
class HelloService : public BBinder {
public:
    status_t onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags = 0) override {
        ALOGI("HelloService called!");
        reply->writeString16(u"Hello from server!");
        return NO_ERROR;
    }
};

client.cpp
sp<IServiceManager> sm = defaultServiceManager();
sp<IBinder> binder = sm->getService(String16("hello"));
Parcel data, reply;
binder->transact(0, data, &reply);
ALOGI("%s", String8(reply.readString16()).string());
```

**補充說明**

-   **AIDL (stable)** 已取代 HIDL 成為新版 HAL IPC 標準。
-   **Native Binder** 仍廣泛用於 system service、daemon，例如 `surfaceflinger`、`audioserver`。
-   Framework Java Binder 與 native Binder 之間可以互通，透過同一個 `/dev/binder` 驅動。
-   所有 IPC 都會經過 **ServiceManager** 註冊與查詢。

## 7. 常見 Debug 工具與節點

| 工具 / 節點 | 功能說明 |
| --- | --- |
| `/dev/binderfs/` | Binder 裝置掛載點，包含 `binder`、`hwbinder`、`vndbinder`。 |
| `/sys/kernel/debug/binder/` | Binder 驅動層的狀態與統計資訊（如進程、thread、node）。 |
| `/proc/<pid>/maps` | 確認進程的 binder mmap 區間。 |
| `service list` | 顯示目前所有已註冊的系統服務。 |
| `dumpsys activity services` | 查看指定系統服務的狀態與連線資訊。 |
| `binder-stats` / `binder-proc` | 顯示 binder driver 的內部統計（交易數量、錯誤等）。 |
| `strace -p <pid>` | 追蹤特定進程對 `/dev/binder` 的 ioctl 呼叫。 |
| `perf trace -e binder:*` | 追蹤 Binder 事件（transaction、reply、thread activity）。 |

## 8. 常見問題與排查

| 問題 | 可能原因 | 修正建議 |
| --- | --- | --- |
| IPC 失敗：`Transaction failed (status -32)` | Server 未註冊服務或 binder handle 遺失 | 確認 `ServiceManager` 已註冊目標服務。 |
| Binder thread 阻塞 | Binder 線程池數量不足或 thread 卡死 | 檢查 `setMaxThreads()` 限制，或查看 ANR trace。 |
| Transaction 過大 | 傳輸資料超過 1MB 限制 | 拆分資料或改用共享記憶體。 |
| `permission denied` | SELinux policy 或 service 權限不足 | 檢查 `.te` 檔與 `service_contexts` 設定。 |
| hwbinder 無回應 | HAL 層 IPC 連線異常 | 檢查 `.hal`、`.rc` 啟動配置與服務狀態。 |
| binder buffer 泄漏 | 未正確釋放引用 | 使用 `BpBinder::decStrong()` 或檢查物件生命周期。 |
| ServiceManager crash | 服務註冊過多或 descriptor 錯誤 | 確認每個服務的 `INTERFACE_DESCRIPTOR` 一致。 |

**小提示**

```bash
# 查看目前所有 Binder 狀態
cat /sys/kernel/debug/binder/state

# 查看特定 PID 的 binder 資訊
cat /sys/kernel/debug/binder/proc/<pid>

# 列出所有系統服務
service list
```

## 9. 學習建議

-   在裝置上執行：

```bash
service list
dumpsys -l
```
-   觀察所有正在運行的 binder 服務。
-   使用 `strace` 監聽應用程式與 `/dev/binder` 的互動。
-   用 `perf trace -e binder:*` 追蹤 binder 事件。
-   閱讀原始碼：
    -   Kernel: `drivers/android/binder.c`
    -   User space: `frameworks/native/libs/binder/`
-   手寫一個簡單的 native binder 範例（client/server pair）以實際理解 transaction。

## 附錄

### A. 延伸閱讀

-   AOSP: `frameworks/native/libs/binder/`
-   Kernel: `drivers/android/binder.c`
-   `Documentation/dev-tools/binderfs.rst`
-   官方 AIDL 文件: https://developer.android.com/guide/components/aidl
