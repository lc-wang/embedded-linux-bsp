
# 🧠 OP-TEE Architecture

## 🎯 本章目的

本章要把以下幾個元件串起來：

```
OP-TEE OS
Trusted Application
Linux TEE driver
/dev/tee0
tee-supplicant
SMC
TF-A BL31
Secure Storage
RPMB
```

重點不是深入 OP-TEE 原始碼，而是先理解：

```
Linux / Android 怎麼呼叫 Secure World？
OP-TEE OS 在整個 boot flow 裡的位置是什麼？
tee-supplicant 為什麼需要存在？
secure storage 為什麼不是單純寫檔案？
```

👉 這一章是理解 Android KeyMint、Gatekeeper、RPMB、secure storage 的基礎。

----------

## 🧭 一張圖先看懂

```
Normal World
+------------------------------------------------+
| Userspace                                      |
|                                                |
|  Client App / HAL                              |
|      ↓                                         |
|  libteec.so                                    |
|      ↓                                         |
|  /dev/tee0                                     |
|      ↓                                         |
|  Linux OP-TEE driver                           |
+------------------------+-----------------------+
                         |
                         | SMC
                         v
EL3 / Secure Monitor
+------------------------------------------------+
| TF-A BL31                                      |
| world switch                                   |
+------------------------+-----------------------+
                         |
                         v
Secure World
+------------------------------------------------+
| OP-TEE OS                                      |
|      ↓                                         |
| Trusted Application                            |
|      ↓                                         |
| secure storage / crypto / key operation        |
+------------------------------------------------+
```

一句話：

```
Normal World 透過 /dev/tee0 → OP-TEE driver → SMC 進入 Secure World。
```

----------

## 1️⃣ OP-TEE 是什麼？

OP-TEE 是：

```
Open Portable Trusted Execution Environment
```

它是一個開源 TEE OS，通常跑在 ARM TrustZone 的 Secure World。

在系統中可以理解成：

```
Linux / Android:
  跑在 Normal World

OP-TEE OS:
  跑在 Secure World

TF-A BL31:
  跑在 EL3，負責 world switch
```

OP-TEE 主要用途：

```
Trusted Application runtime
secure storage
crypto operation
key management
RPMB access
Android KeyMint / Gatekeeper backend
```

----------

## 2️⃣ OP-TEE 在 Boot Flow 的位置

典型 ARM 平台 boot flow：

```
BootROM
  ↓
TF-A BL2 / SPL
  ↓
TF-A BL31
  ↓
OP-TEE OS
  ↓
U-Boot
  ↓
Linux / Android
```

可以簡化成：

```
[BootROM]
    ↓
[Load secure firmware]
    ↓
[Load OP-TEE]
    ↓
[Load U-Boot]
    ↓
[Boot Linux]
```

重點：

```
OP-TEE 必須在 Linux 之前被載入。
Linux 之後只是透過 SMC 呼叫已經存在的 OP-TEE。
```

----------

## 3️⃣ OP-TEE 不是 Root of Trust 本身

OP-TEE 很重要，但不要把它誤解成最底層的 Root of Trust。

正確關係是：

```
BootROM / eFuse
  ↓ verify
TF-A / OP-TEE image
  ↓
OP-TEE becomes trusted runtime
```

也就是：

```
OP-TEE 是否可信，
取決於前面的 secure boot chain 是否有驗證 OP-TEE image。
```

如果 OP-TEE image 沒有被驗證：

```
Secure World 裡跑的東西也不一定可信。
```

----------

## 4️⃣ Normal World 呼叫 OP-TEE 的流程

以 Linux userspace 呼叫 Trusted Application 為例：

```
Client App
  ↓
libteec.so
  ↓
/dev/tee0
  ↓
Linux OP-TEE driver
  ↓
SMC
  ↓
TF-A BL31
  ↓
OP-TEE OS
  ↓
Trusted Application
```

更工程一點看：

```
userspace ioctl()
  ↓
drivers/tee/tee_core.c
  ↓
drivers/tee/optee/
  ↓
SMC conduit
  ↓
secure monitor
  ↓
OP-TEE syscall / TA dispatch
```

重點：

```
Normal World 不直接執行 TA。
它只是透過 TEE driver 發 request。
```

----------

## 5️⃣ `/dev/tee0` 和 `/dev/teepriv0`

Linux OP-TEE driver probe 成功後，通常會看到：

```
ls -l /dev/tee*
```

可能出現：

```
/dev/tee0/dev/teepriv0
```

常見理解：


| Device node | 用途 |  
|-------------|------|  
| `/dev/tee0` | 一般 client app 使用，用來呼叫 TA |  
| `/dev/teepriv0` | Privileged supplicant 使用，通常給 `tee-supplicant` |

簡化 flow：

```
Normal App
  ↓
/dev/tee0
  ↓
OP-TEE driver
```

```
tee-supplicant
  ↓
/dev/teepriv0
  ↓
OP-TEE driver
```

----------

## 6️⃣ tee-supplicant 是什麼？

tee-supplicant 是 Normal World userspace daemon。

它的角色是：

```
幫 Secure World 做它自己不能直接做的 Normal World 操作。
```

例如：

```
存取 Linux filesystem
讀寫 REE filesystem secure storage
協助載入 TA
協助 RPMB access flow
提供某些 normal world resource
```

為什麼 OP-TEE 需要它？

因為 Secure World 不應該直接管理 Linux filesystem，也不應該直接依賴 normal world driver 細節。

所以架構變成：

```
OP-TEE OS
  ↓ request
Linux OP-TEE driver
  ↓ wake up
tee-supplicant
  ↓ perform normal world operation
  ↓ response
OP-TEE OS
```

----------

## 7️⃣ tee-supplicant Flow

以 secure storage 存檔為例：

```
Trusted Application
  ↓
OP-TEE secure storage API
  ↓
OP-TEE OS
  ↓
需要 Normal World filesystem access
  ↓
RPC to Linux OP-TEE driver
  ↓
wake up tee-supplicant
  ↓
tee-supplicant writes encrypted object
  ↓
return result to OP-TEE
```

簡化圖：

```
[TA]
  ↓
[OP-TEE OS]
  ↓ RPC
[Linux OP-TEE driver]
  ↓
[tee-supplicant]
  ↓
[REE filesystem / RPMB]
```

重點：

```
tee-supplicant 看到的是 encrypted object。
真正的 key operation 應該留在 Secure World。
```

----------

## 8️⃣ Trusted Application 是什麼？

Trusted Application，簡稱 TA。

它是跑在 OP-TEE OS 裡的安全應用。

常見用途：

```
crypto service
key management
secure counter
device identity
attestation helper
Android KeyMint backend
Gatekeeper backend
```

對 Normal World 來說，TA 通常透過 UUID 被呼叫。

概念：

```
Client App
  ↓ open session by UUID
Trusted Application
  ↓ execute command
return result
```

----------

## 9️⃣ Client App / TA 基本模型

Normal World client app 通常會做：

```
TEEC_InitializeContext()
TEEC_OpenSession()
TEEC_InvokeCommand()
TEEC_CloseSession()
TEEC_FinalizeContext()
```

概念 flow：

```
[Client App]
    ↓ initialize context
[TEE Device]
    ↓ open session
[TA UUID]
    ↓ invoke command
[TA command handler]
    ↓ return result
[Client App]
```

重點：

```
Client App 是 Normal World。
TA 是 Secure World。
兩者中間透過 TEE Client API 與 OP-TEE driver 溝通。
```

----------

## 🔟 Secure Storage 是什麼？

Secure Storage 是 OP-TEE 常見功能之一。

它用來保存：

```
key material
certificate
secure counter
TA private data
device secret
attestation data
```

但要注意：

```
secure storage 不等於普通檔案加密而已。
```

它通常需要保證：

```
confidentiality
integrity
anti-rollback
device binding
```

----------

## 1️⃣1️⃣ REE FS vs RPMB Secure Storage

OP-TEE secure storage 常見 backend：

```
REE filesystem
RPMB
```

----------

### REE filesystem

REE 是 Rich Execution Environment，也就是 Normal World。

REE FS 模式下，加密後的 secure object 可能存放在 Linux filesystem。

```
OP-TEE encrypt / protect object
  ↓
tee-supplicant
  ↓
Linux filesystem
```

優點：

```
容易整合不一定需要 RPMB hardware
```

限制：

```
anti-rollback 能力較弱
依賴 Normal World storage
```

----------

### RPMB

RPMB 是 Replay Protected Memory Block。

常見於 eMMC / UFS。

```
OP-TEE secure storage
  ↓
RPMB authenticated access
  ↓
replay protection
```

優點：

```
較適合保存 secure counter / anti-rollback data
可防止舊資料被 replay
```

限制：

```
需要 RPMB key provisioning
需要 storage device 支援
BSP bring-up 較複雜
```

----------

## 1️⃣2️⃣ OP-TEE 與 RPMB 的關係

RPMB 通常不是直接給 Linux app 任意操作。

典型 flow：

```
Trusted Application
  ↓
OP-TEE OS
  ↓
RPMB secure storage layer
  ↓
RPC to tee-supplicant / kernel
  ↓
eMMC / UFS RPMB command
  ↓
RPMB partition
```

重點：

```
Normal World 可以協助傳輸 RPMB command，
但不應該掌握 RPMB key。
```

----------

## 1️⃣3️⃣ Android BSP 常見對應

在 Android 中，OP-TEE 常作為下列 HAL 的安全後端：

```
KeyMint
Gatekeeper
StrongBox-like key operation
DRM / Widevine secure service
secure storage
rollback index storage
```

簡化 flow：

```
Android Framework
  ↓
Keystore / Gatekeeper
  ↓
HAL
  ↓
TEE Client API
  ↓
Linux OP-TEE driver
  ↓
OP-TEE TA
  ↓
secure key operation
```

BSP 工程師常遇到的問題：

```
KeyMint HAL 起來但 TA 找不到
Gatekeeper enrollment fail
RPMB key 沒 provision
tee-supplicant 沒啟動
/dev/tee0 不存在
OP-TEE memory reserve 錯誤
```

----------

## 1️⃣4️⃣ Linux Kernel 相關元件

Linux kernel 中常見路徑：

```
drivers/tee/
drivers/tee/optee/
```

常見 config：

```
CONFIG_TEE
CONFIG_OPTEE
```

常見 device tree / firmware 關聯：

```
firmware {
    optee {
        compatible = "linaro,optee-tz";
        method = "smc";
    };
};
```

重點：

```
method = "smc" 或 "hvc" 要和 firmware conduit 一致。
```

如果 Linux 期待 SMC，但 firmware 使用 HVC，可能會導致 OP-TEE probe 失敗。

----------

## 1️⃣5️⃣ BSP Debug：確認 OP-TEE 是否正常

### Step 1：看 kernel log

```
dmesg | grep -i optee
dmesg | grep -i tee
```

常見成功方向：

```
optee: probing for conduit method
optee: revision 3.x
optee: initialized driver
```

----------

### Step 2：看 device node

```
ls -l /dev/tee*
```

預期可能看到：

```
/dev/tee0
/dev/teepriv0
```

如果沒有：

```
Linux OP-TEE driver 可能沒 probe
Device Tree firmware node 可能缺失
OP-TEE image 可能沒被載入
SMC conduit 可能不一致
```

----------

### Step 3：確認 tee-supplicant

```
ps | grep tee-supplicant
systemctl status tee-supplicant
```

如果 tee-supplicant 沒起來，可能影響：

```
REE FS secure storage
TA loading
RPMB access
Android KeyMint / Gatekeeper flow
```

----------

### Step 4：確認 reserved memory

```
dmesg | grep -i reserved
cat /proc/iomem
```

確認 OP-TEE secure memory 沒被 Linux 當成一般 RAM 使用。

----------

### Step 5：確認 kernel config

```
zcat /proc/config.gz | grep -E "TEE|OPTEE"
```

預期：

```
CONFIG_TEE=y
CONFIG_OPTEE=y
```

----------

## 1️⃣6️⃣ 常見錯誤

### ❌ OP-TEE image 沒被載入

Boot flow 少了 tee.bin / tee.elf。

結果：

```
Linux 找不到 OP-TEE
/dev/tee0 不存在
KeyMint / Gatekeeper 失敗
```

----------

### ❌ Device Tree firmware node 錯

例如缺少：

```
firmware {
    optee {
        compatible = "linaro,optee-tz";
        method = "smc";
    };
};
```

結果：

```
Linux OP-TEE driver 不知道如何呼叫 secure world。
```

----------

### ❌ SMC / HVC method 不一致

Device Tree 寫：

```
method = "smc"
```

但 firmware 實際使用 HVC，或相反。

結果可能是：

```
optee probe fail
SMC call failed
api uid mismatch
system hang
```

----------

### ❌ reserved-memory 設錯

OP-TEE 使用的 secure memory 沒有保留，或大小 / base address 錯誤。

結果可能是：

```
OP-TEE crash
Linux memory corruption
random boot failure
secure service unstable
```

----------

### ❌ tee-supplicant 沒啟動

結果可能是：

```
TA loading failed
secure storage failed
RPMB access failed
KeyMint / Gatekeeper failed
```

----------

### ❌ RPMB key 沒 provision

如果 secure storage backend 使用 RPMB，但 RPMB key 沒正確燒錄。

結果可能是：

```
RPMB authentication failed
secure storage init failed
rollback counter unavailable
Android keystore / KeyMint issue
```

----------

## 1️⃣7️⃣ OP-TEE Debug Checklist

```
[ ] OP-TEE image is included in boot flow
[ ] OP-TEE image is verified by secure boot chain
[ ] TF-A BL31 is loaded before Linux
[ ] Device Tree has firmware optee node
[ ] SMC / HVC conduit matches firmware
[ ] OP-TEE reserved memory is correct
[ ] CONFIG_TEE is enabled
[ ] CONFIG_OPTEE is enabled
[ ] /dev/tee0 exists
[ ] /dev/teepriv0 exists if needed
[ ] tee-supplicant is running
[ ] TA files are installed if dynamic TA loading is used
[ ] secure storage backend is configured
[ ] RPMB key is provisioned if RPMB is used
[ ] Android HAL can find required TA
```
