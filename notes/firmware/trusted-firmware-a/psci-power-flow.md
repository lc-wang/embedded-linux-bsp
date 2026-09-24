# Trusted Firmware-A PSCI Power Flow 架構解析

在 Trusted Firmware-A（TF-A）所提供的所有 runtime services 中，  
**PSCI（Power State Coordination Interface）是最核心、也最不可或缺的一項。**

對作業系統而言：

- CPU 開機（CPU_ON）
- CPU 關機（CPU_OFF）
- suspend / resume
- system shutdown / reset

**全部都仰賴 PSCI。**

而 PSCI 的實作位置，正是 **BL31（EL3 Secure Monitor）**。

本章目的在於說明：

- PSCI 在整個 boot / runtime 中的角色
- PSCI 與 BL31 的關係
- CPU power flow 的實際執行路徑
- Secure / Non-secure world 的責任分工
- 為何 power 問題幾乎一定要 debug BL31

## 1. 什麼是 PSCI？

PSCI 是 ARM 定義的一套標準介面，用於：

> **由 non-secure world 請求 secure world 協調 power state。**

其設計前提是：

- power state 是高度平台相關的
- 但 OS 不應直接操作 secure / platform 資源
- 因此由 EL3（BL31）作為唯一仲裁者

## 2. PSCI 在整體架構中的位置

整體關係如下：
```text
Linux / U-Boot (non-secure)
│
│ SMC (PSCI call)
▼
BL31 (EL3 Secure Monitor)
│
│ platform power ops
▼
SoC power / clock / reset controller
```

關鍵點：

> **Non-secure world 永遠不直接碰 power controller。**

## 3. PSCI 與 SMC 的關係

PSCI 是透過 **SMC（Secure Monitor Call）** 進入 BL31。

流程概念：
```text
EL1/EL2 (OS)
└─ SMC #PSCI_CPU_ON
│
▼
EL3 (BL31)
└─ psci handler
│
▼
platform power implementation
```

BL31 會根據 SMC ID 判斷該呼叫是否屬於 PSCI，並進行分派。

## 4. PSCI 支援的基本功能

常見 PSCI function 包括：

- CPU_ON
- CPU_OFF
- CPU_SUSPEND
- SYSTEM_OFF
- SYSTEM_RESET

不同平台可支援的功能組合可能不同，但 API 定義是標準化的。

## 5. 為什麼 CPU Power 必須由 BL31 處理？

原因包括：

1. CPU power 涉及 secure 資源
2. 涉及多核心同步（race condition）
3. 涉及 power domain / cluster 狀態
4. 需要避免 non-secure world 破壞系統穩定性

因此設計上：

> **只有 EL3 才能安全地協調 CPU power 狀態。**

## 6. PSCI 的責任分層模型

PSCI 的實作可分為三層：
```text
PSCI generic layer
│
├─ state validation
├─ parameter checking
│
platform power ops
│
├─ CPU on/off
├─ cluster power control
│
SoC hardware control
```

TF-A 本身負責前兩層，
最底層的實作由 platform code 提供。

## 7. CPU_ON 的典型流程

以 CPU_ON 為例，流程概念如下：
```text
Linux kernel
└─ psci_cpu_on()
│
├─ SMC call
▼
BL31
├─ validate target CPU
├─ check power state
├─ setup entry point
├─ call platform_cpu_on()
│
▼
SoC power controller
├─ power up CPU
├─ release reset
└─ jump to entry address
```
此流程中：

- entry point 通常是 secondary CPU boot code
- secure / non-secure 狀態由 BL31 決定

## 8. Secondary CPU 為何一定從 BL31 進入？

Secondary CPU 啟動時：

- 尚未進入 OS
- 尚未建立完整 context
- 需要 secure world 初始化

因此其 entry path 為：
```text
power on
→ secure reset vector
→ BL31
→ non-secure entry
```

而不是直接跳進 kernel。

## 9. CPU_OFF / CPU_SUSPEND 的差異

### 9.1 CPU_OFF

- CPU 完全關閉
- context 不保留
- 再次啟動需 CPU_ON

### 9.2 CPU_SUSPEND

- CPU 進入低功耗狀態
- context 可能被保存
- resume 後回到原執行點

BL31 必須確保：

- secure context 正確保存 / 還原
- non-secure world 無法破壞流程

## 10. System Power State（SYSTEM_OFF / RESET）

SYSTEM_OFF / RESET 代表：

- 整個 system 的 power flow
- 通常觸發 PMIC 或 SoC reset controller

流程為：
```text
OS
└─ SMC SYSTEM_RESET
│
▼
BL31
└─ platform_system_reset()
│
▼
Hardware reset
```

此類操作**必須由 secure world 控制**。

## 11. PSCI 與 BL31 Console 的關係

在實務 debug 中：

- CPU 無法啟動
- secondary core 無反應
- suspend/resume 卡死

**PSCI console log 是關鍵線索。**

BL31 console 常用於：

- 確認 PSCI handler 是否被呼叫
- 確認 power ops 是否執行
- 分辨是 OS 問題或 platform 問題

## 12. 常見問題與排查（常見 PSCI 問題類型）

實務上最常見的問題包括：

- CPU_ON 返回成功但 CPU 未啟動
- CPU_OFF 後 CPU 無法再次啟動
- suspend/resume 卡死
- power domain 設定錯誤
- entry point address 錯誤

這些問題大多集中於：

- BL31 platform power ops
- power controller driver
- memory / MMU context 保存
