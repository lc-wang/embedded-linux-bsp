
# 🧠 ARM TrustZone Overview

## 🎯 本章目的

本章要把以下幾個概念串起來：

```
ARM TrustZone
Secure World
Normal World
EL3 / Secure Monitor
SMC
TF-A
OP-TEE
Linux / Android
```

重點不是深入 OP-TEE implementation，而是先理解：

```
Linux / Android 為什麼不是系統中最高權限？
為什麼有些 key / memory / device 不能被 Linux 直接存取？
Normal World 要怎麼呼叫 Secure World？
```

----------

## 🧭 一張圖先看懂

```
+------------------------------------------------+
|                 Normal World                   |
|                                                |
|  EL0: Linux / Android userspace                |
|       app / daemon / tee-supplicant            |
|                                                |
|  EL1: Linux kernel                             |
|       driver / optee driver                    |
+------------------------+-----------------------+
                         |
                         | SMC
                         v
+------------------------------------------------+
|                    EL3                         |
|              Secure Monitor / TF-A BL31        |
|              world switch handler              |
+------------------------+-----------------------+
                         |
                         v
+------------------------------------------------+
|                 Secure World                   |
|                                                |
|  OP-TEE OS                                     |
|  Trusted Application                           |
|  secure storage / crypto / key service         |
+------------------------------------------------+
```

一句話：

```
Linux 在 Normal World。
OP-TEE 在 Secure World。
兩邊透過 SMC 經過 EL3 切換。
```

----------

## 1️⃣ TrustZone 是什麼？

ARM TrustZone 是 ARM SoC 提供的硬體隔離機制。

它把系統分成兩個世界：


| World | 說明 | 常見內容 |  
|-------|------|----------|  
| Normal World | 一般系統執行環境 | Linux / Android / userspace / normal driver |  
| Secure World | 安全執行環境 | OP-TEE / secure service / Trusted Application |

重點：

```
Secure World 可以保護 key、secure storage、crypto service。
Normal World 不能直接讀取 Secure World 的 memory 或 secure peripheral。
```

----------

## 2️⃣ 為什麼 BSP 工程師需要懂？

因為在 ARM BSP / Android BSP 中，你會常看到：

```
TF-A
BL31
OP-TEE
SMC
reserved-memory
secure memory
RPMB
KeyMint
Gatekeeper
secure storage
```

很多問題都跟 TrustZone 有關：

```
為什麼 Linux 讀不到某段 memory？
為什麼某些 register access 會 abort？
為什麼 Android KeyMint 需要 TEE？
為什麼 RPMB 不能只靠 normal userspace 管理？
為什麼 boot flow 要載入 bl31.bin / tee.bin？
```

----------

## 3️⃣ ARM Exception Level 對應

ARMv8-A 常見 Exception Level：


### ARM Exception Level 與 TrustZone 對應

| Level | Normal World | Secure World / Firmware |
|--------|--------------|--------------------------|
| EL0 | userspace app | Trusted Application |
| EL1 | Linux kernel | OP-TEE OS |
| EL2 | KVM / Hypervisor | Secure EL2（視平台而定） |
| EL3 | 不屬於 Linux | Secure Monitor / TF-A BL31 |

簡化理解：

```
Linux kernel 通常跑在 EL1。
OP-TEE OS 通常跑在 Secure EL1。
TF-A BL31 跑在 EL3。
```

----------

## 4️⃣ Boot Flow 裡 TrustZone 的位置

典型 ARM secure boot flow：

```
BootROM
  ↓
TF-A BL2 / SPL
  ↓
TF-A BL31
  ↓
OP-TEE
  ↓
U-Boot
  ↓
Linux / Android
```

可以理解成：

```
BootROM:
  SoC 最早執行的 ROM code

BL2 / SPL:
  初始化 DRAM，載入後續 firmware

BL31:
  EL3 secure monitor，負責 world switch

OP-TEE:
  Secure World OS

U-Boot:
  Normal World bootloader

Linux / Android:
  Normal World OS
```

----------

## 5️⃣ SMC 是什麼？

SMC 是：

```
Secure Monitor Call
```

Normal World 不能直接跳進 Secure World。

它必須透過 SMC：

```
Linux driver
  ↓
SMC instruction
  ↓
EL3 Secure Monitor
  ↓
OP-TEE
  ↓
Trusted Application
```

所以 SMC 可以理解成：

```
Normal World 呼叫 Secure World 的入口。
```

----------

## 6️⃣ Linux 呼叫 OP-TEE 的流程

以 Linux 使用 OP-TEE service 為例：

```
userspace
  ↓
/dev/tee0
  ↓
Linux optee driver
  ↓
SMC
  ↓
TF-A BL31
  ↓
OP-TEE OS
  ↓
Trusted Application
```

更完整一點：

```
+-----------------------------+
| normal userspace            |
| tee-supplicant / app        |
+-------------+---------------+
              |
              v
+-----------------------------+
| Linux OP-TEE driver         |
| drivers/tee/optee/          |
+-------------+---------------+
              |
              | SMC
              v
+-----------------------------+
| TF-A BL31 / Secure Monitor  |
+-------------+---------------+
              |
              v
+-----------------------------+
| OP-TEE OS                   |
| Trusted Application         |
+-----------------------------+
```

----------

## 7️⃣ Secure Memory 是什麼？

TrustZone 不只隔離 CPU 執行狀態，也常搭配 memory protection。

常見情況：

```
DRAM 中保留一段 secure memory
只有 Secure World 可以存取
Normal World Linux 不可使用
```

Device Tree 可能會看到：

```
reserved-memory {
    optee@9e000000 {
        reg = <0x0 0x9e000000 0x0 0x02000000>;
        no-map;
    };
};
```

重點：

```
no-map 代表 Linux 不應該把這段 memory map 起來使用。
```

----------

## 8️⃣ Secure Peripheral 是什麼？

有些 SoC peripheral 可以設定成：

```
secure only
non-secure only
shared
```

例如：

```
crypto engine
efuse controller
key storage
secure timer
secure watchdog
some GPIO / I2C / SPI controller
```

如果 peripheral 被設定成 secure only，Normal World driver 直接 access 可能會：

```
bus error
external abort
permission fault
system hang
```

----------

## 9️⃣ OP-TEE 在 TrustZone 裡的角色

OP-TEE 是 Secure World 裡的 TEE OS。

它通常提供：

```
Trusted Application runtime
secure storage
crypto service
RPMB access
key management
Android KeyMint / Gatekeeper backend
```

但要注意：

```
TrustZone 是硬體隔離機制。
OP-TEE 是跑在 Secure World 的軟體。
```

不要把兩者混成同一個東西。

----------

## 🔟 Android BSP 常見關聯

在 Android BSP 中，TrustZone 常出現在：

```
KeyMint
Gatekeeper
Widevine
RPMB
AVB rollback index
hardware-backed keystore
```

簡化 flow：

```
Android framework
  ↓
KeyMint HAL
  ↓
TEE client API
  ↓
Linux OP-TEE driver
  ↓
SMC
  ↓
OP-TEE Trusted Application
  ↓
secure key operation
```

重點：

```
key 不一定會離開 Secure World。
Normal World 只是要求 Secure World 幫忙做 crypto operation。
```

----------

## 1️⃣1️⃣ BSP Debug：怎麼確認 OP-TEE / TrustZone 有起來？

### 看 kernel log

```
dmesg | grep -i optee
dmesg | grep -i tee
```

常見 log：

```
optee: probing for conduit method
optee: revision 3.x
optee: initialized driver
```

----------

### 看 device node

```
ls -l /dev/tee*
```

可能看到：

```
/dev/tee0
/dev/teepriv0
```

代表 Linux OP-TEE driver 已建立 TEE device。

----------

### 看 reserved memory

```
dmesg | grep -i reserved
cat /proc/iomem
```

確認 OP-TEE secure memory 是否被保留。

----------

### 看 kernel config

```
zcat /proc/config.gz | grep OPTEE
```

常見 config：

```
CONFIG_TEE=y
CONFIG_OPTEE=y
```

----------

## 1️⃣2️⃣ 常見錯誤

### ❌ 沒載入 OP-TEE image

Boot flow 少載入 tee.bin / tee.elf：

```
BootROM → TF-A → U-Boot → Linux
```

結果：

```
Linux 找不到 OP-TEE。
Android KeyMint / Gatekeeper 可能失敗。
```

----------

### ❌ reserved-memory 設錯

secure memory 沒有保留，Linux 把 OP-TEE memory 拿去用。

可能結果：

```
OP-TEE crash
random memory corruption
SMC call failed
kernel boot unstable
```

----------

### ❌ SMC conduit 不一致

Linux 與 firmware 對 SMC / HVC conduit 認知不同。

可能 log：

```
optee: api uid mismatch
optee: probing for conduit method failed
```

----------

### ❌ secure peripheral 被 Linux driver 直接操作

某些 register 被設成 secure access only。

Normal World driver 直接讀寫可能造成：

```
Synchronous External Abort
permission fault
hang
```

----------

### ❌ Android HAL 找不到 TEE backend

Android userspace HAL 啟動，但底層 TEE service 不存在。

常見影響：

```
KeyMint fail
Gatekeeper fail
keystore fail
Widevine fail
```
