# Trusted Firmware-A BL31 Runtime Services 架構解析

在 TF-A 的多階段架構中，  
**BL31 是唯一「長期常駐」且「持續參與系統運作」的韌體階段**。

與 BL2 的角色不同：

- BL2：一次性 loader，完成任務後即消失
- **BL31：EL3 Secure Monitor，會在系統整個生命週期中持續存在**

本章目的在於說明：

- BL31 在整體 boot flow 中的定位
- 什麼是 runtime service
- BL31 如何成為 secure / non-secure world 的仲裁者
- 為何所有 non-secure world 的控制權都必須經過 BL31
- BL31 與 console、MMU、PSCI 的關係

## 1. BL31 在 Boot Flow 中的位置

回顧整體流程：
```
Boot ROM
│
▼
BL1 (optional)
│
▼
BL2 ── image loader
│
▼
BL31 ── EL3 runtime firmware ← 本章重點
│
▼
BL33 ── U-Boot / OS
```

關鍵差異在於：
> **BL31 不只是過渡階段，而是 secure world 的常駐控制核心。**

## 2. BL31 的核心角色總覽

BL31 同時扮演三個角色：

1. **EL3 Secure Monitor**
2. **Runtime Service Dispatcher**
3. **Secure / Non-secure World 仲裁者**

這三個角色構成 TF-A 的核心價值。

## 3. 什麼是 EL3 Secure Monitor？

在 ARMv8-A 架構中：

- EL0 / EL1：Userspace / Kernel
- EL2：Hypervisor
- **EL3：Secure Monitor（最高權限）**

EL3 的特性：

- 能存取 secure 與 non-secure 資源
- 能切換 security state
- 能攔截 world switch
- 能處理 SMC（Secure Monitor Call）

BL31 正是執行於 **EL3** 的韌體。

## 4. Runtime Service 的概念

在 TF-A 中，runtime service 指的是：

> **由 BL31 提供、透過 SMC 呼叫的 secure world 服務。**

這些服務包括但不限於：

- PSCI（power management）
- secure timer
- trusted OS interface（BL32 存在時）
- platform-specific secure service

## 5. SMC（Secure Monitor Call）基本模型

Non-secure world（例如 Linux kernel）透過：

SMC instruction

請求 BL31 提供服務。

流程概念如下：
```
Non-secure world (EL1/EL2)
│
├─ SMC
│
▼
BL31 (EL3)
│
├─ dispatch runtime service
│
▼
Return to non-secure world
```

## 6. Runtime Service Dispatcher 架構

BL31 內部維護一組 runtime service registry：

- 每個 service 有對應的 SMC range
- 每個 service 提供 handler function
- BL31 根據 SMC ID 進行分派

因此 BL31 本身：

- 不實作所有功能
- 而是作為 **secure service 的 dispatcher**

## 7. PSCI：最重要的 Runtime Service

PSCI（Power State Coordination Interface）是：

- 最廣泛使用的 runtime service
- 幾乎所有 OS 都依賴它
- 控制 CPU on/off、suspend、resume

其關鍵特性是：

> **只有 EL3 才能安全地控制 CPU power state。**

因此 PSCI 必須由 BL31 提供。

## 8. BL31 與 BL33（U-Boot / OS）的關係

BL31 與 BL33 的關係並非「上下層程式」，
而是：

> **Secure Monitor 與 Client OS 的關係。**

BL33：

- 執行於 non-secure world
- 無法直接操作 secure 資源
- 必須透過 SMC 呼叫 BL31

## 9. 為何 BL31 必須先於 BL33 啟動？

原因包括：

- 建立 secure memory protection
- 初始化 runtime service
- 設定 exception vector
- 準備 power management 機制
- 建立 secure / non-secure world 切換邏輯

因此：

> **BL33 永遠不可能繞過 BL31 直接啟動。**

## 10. BL31 的記憶體與 MMU 特性

- BL31 有獨立的 MMU 與 page table
- secure memory 與 non-secure memory 嚴格區分
- runtime service handler 執行於 secure memory

這確保：

- non-secure world 無法竄改 BL31
- runtime service 具備完整隔離性

## 11. BL31 與 Console 的角色

- BL31 會重新註冊 console
- console framework 僅存在於 BL31 image
- UART 硬體狀態由 BL2 繼承而來

BL31 console 的主要用途是：

- secure runtime debug
- early PSCI / SMC 問題分析
- secure world exception 輸出

## 12. BL31 的生命週期特性

與 BL2 最大的不同在於：

| 項目 | BL2 | BL31 |
|------|-----|------|
| 是否常駐 | 否 | 是 |
| 是否提供 runtime service | 否 | 是 |
| 是否處理 SMC | 否 | 是 |
| 是否參與 power management | 否 | 是 |

因此：

> **BL31 的穩定性直接影響整個系統的可靠性。**

## 13. 常見問題與排查（常見 BL31 相關問題）

實務上常見的 BL31 問題包括：

- SMC 呼叫直接 hang
- PSCI 呼叫失敗導致 CPU 無法啟動
- secure / non-secure memory mapping 錯誤
- exception vector 設定錯誤
- BL31 console 有字但 OS 無法啟動

這些問題通常與：

- runtime service 初始化
- MMU / memory layout
- platform-specific secure code

有關。
