
# 🧠 Root of Trust

本章節整理 Platform Security 中最核心的概念：

> **Root of Trust 是整個系統安全信任鏈的起點。**

在 Secure Boot、Trusted Boot、TPM、OP-TEE、AVB、Yocto image signing、rollback protection 等主題中，Root of Trust 都是最底層的基礎。

----------

## 1. 為什麼工程師需要理解 Root of Trust？

在一般 bring-up 階段，我們通常關心：

```
BootROM
  ↓
SPL / TF-A
  ↓
U-Boot
  ↓
Kernel
  ↓
RootFS / Android
```

但從 Security 的角度來看，問題不是只有「能不能開機」，而是：

```
這個系統是不是從第一行程式開始就是可信的？
```

也就是說：

-   Bootloader 是不是被竄改？
-   Kernel image 是不是正確版本？
-   Device Tree 是否被替換？
-   RootFS 是否被植入惡意檔案？
-   Firmware update 是否可能被降版攻擊？
-   金鑰是不是安全保存？
-   Secure storage 是否真的不可被 Linux userspace 任意讀寫？

這些問題的起點，都是 **Root of Trust**。

----------

## 2. Root of Trust 是什麼？

Root of Trust 可以理解成：

> 系統中最早被信任、且無法被一般軟體修改的安全基礎。

它通常來自硬體或 SoC 內部不可變的元件，例如：

```
SoC BootROM
eFuse / OTP
Hardware Unique Key
Secure key storage
TPM
Secure Element
TEE / TrustZone secure world
```

Root of Trust 本身不是單一功能，而是一組安全能力的起點。

----------

## 3. Root of Trust 的核心概念

Root of Trust 通常包含三種能力：

```
Root of Trust for Verification
Root of Trust for Measurement
Root of Trust for Storage
```

----------

## 4. Root of Trust for Verification

### 4.1 概念

Root of Trust for Verification 負責確認：

```
下一階段要執行的程式碼是不是可信的？
```

最常見的應用就是 **Secure Boot**。

### 4.2 Boot Flow

```
+-----------------------------+
| SoC BootROM                 |
| Immutable / Mask ROM        |
+-----------------------------+
              │
              │ verify signature
              ▼
+-----------------------------+
| First Stage Bootloader      |
| SPL / TF-A BL2              |
+-----------------------------+
              │
              │ verify signature
              ▼
+-----------------------------+
| Second Stage Bootloader     |
| U-Boot / TF-A BL31 / BL33   |
+-----------------------------+
              │
              │ verify signature
              ▼
+-----------------------------+
| Linux Kernel + DTB          |
+-----------------------------+
              │
              │ verify rootfs / vbmeta
              ▼
+-----------------------------+
| RootFS / Android System     |
+-----------------------------+
```

這條鏈稱為：

```
Chain of Trust
```

意思是：

```
前一階段驗證下一階段
```

只要其中一層沒有驗證，整條安全鏈就會中斷。

----------

## 5. Root of Trust for Measurement

### 5.1 概念

Root of Trust for Measurement 不一定阻止系統開機，而是記錄：

```
系統實際載入了什麼？
```

這通常稱為：

```
Measured Boot
```

Measured Boot 的目的不是「阻擋」，而是「留下證據」。

----------

### 5.2 Secure Boot vs Measured Boot

```
Secure Boot:
  不可信 → 不讓你開機

Measured Boot:
  先記錄你載入了什麼
  之後讓遠端或本地 policy 判斷是否可信
```

----------

### 5.3 Measured Boot Flow

```
+-----------------------------+
| BootROM / Firmware          |
+-----------------------------+
              │
              │ measure hash
              ▼
+-----------------------------+
| TPM PCR                     |
| Platform Configuration Reg  |
+-----------------------------+
              │
              │ measure hash
              ▼
+-----------------------------+
| Bootloader                  |
+-----------------------------+
              │
              │ measure hash
              ▼
+-----------------------------+
| Kernel / Initrd / Cmdline   |
+-----------------------------+
```

每一階段會把下一階段的 hash extend 到 TPM PCR 中。

----------

## 6. Root of Trust for Storage

### 6.1 概念

Root of Trust for Storage 負責保護敏感資料，例如：

```
private key
disk encryption key
RPMB key
device identity key
attestation key
secure counter
rollback index
```

這些資料不能只放在普通 flash 或 rootfs 中，否則攻擊者可以直接 dump 或修改。

----------

### 6.2 常見實作

```
eFuse / OTP
TPM NVRAM
Secure Element
TEE secure storage
RPMB
SoC internal key ladder
Hardware Unique Key
```

----------

## 7. Root of Trust 在 SoC Boot Flow 的位置

以 ARM SoC 為例，常見 boot flow 如下：

```
Power On
  ↓
SoC BootROM
  ↓
Read boot media
  ↓
Verify first bootloader
  ↓
Load TF-A / SPL
  ↓
Initialize DRAM
  ↓
Load U-Boot
  ↓
Load Kernel / DTB / initramfs
  ↓
Mount RootFS
```

Security 視角會變成：

```
Power On
  ↓
Immutable BootROM
  ↓
Use key hash from eFuse / OTP
  ↓
Verify signed bootloader
  ↓
Bootloader verifies next image
  ↓
Kernel verifies rootfs / dm-verity / AVB
  ↓
System enters trusted runtime
```

----------

## 8. 工程師最常遇到的 Root of Trust 元件

### 8.1 BootROM

BootROM 通常是 SoC 內部不可修改的 ROM code。

它負責：

```
選擇 boot source
載入第一階段 bootloader
檢查 boot mode pin
讀取 eFuse / OTP 設定
驗證 bootloader 簽章
```

如果 BootROM 支援 Secure Boot，通常它會使用 eFuse 中的 key hash 來驗證 bootloader。

----------

### 8.2 eFuse / OTP

eFuse 或 OTP 通常用來保存不可逆設定，例如：

```
Secure Boot enable bit
Public key hash
Rollback index
Debug lock state
JTAG disable bit
Device lifecycle state
```

常見狀態：

```
Open
  ↓
Provisioned
  ↓
Secure
  ↓
Closed / Production
```

一旦燒錄錯誤，通常很難恢復，所以要特別小心。

----------

### 8.3 Public Key Hash

Secure Boot 通常不會把完整 public key 燒進 eFuse，而是燒錄：

```
Hash(public key)
```

BootROM 載入 image 之後會檢查：

```
image 內的 public key hash
是否等於 eFuse 中保存的 public key hash
```

如果一致，才使用該 public key 驗證 image signature。

----------

### 8.4 Hardware Unique Key

Hardware Unique Key 通常是 SoC 內部每顆晶片不同的 key。

用途包含：

```
device binding
key derivation
secure storage encryption
RPMB key derivation
disk encryption key wrapping
```

這種 key 通常不應該被 normal world 直接讀出。

----------

### 8.5 TPM

TPM 可以作為 Root of Trust for Measurement 與 Root of Trust for Storage。

常見用途：

```
Measured Boot
Remote Attestation
Sealing key to PCR state
Device identity
Secure key storage
```

在 x86 平台較常見，在 embedded ARM 平台也可能透過 discrete TPM 或 firmware TPM 實作。

----------

### 8.6 OP-TEE / TrustZone

OP-TEE 本身不是最原始的 Root of Trust，但它常常是 Root of Trust 的延伸。

OP-TEE 可以提供：

```
secure storage
key management
RPMB access
cryptographic service
trusted application runtime
```

但要注意：

```
OP-TEE 是否可信
取決於前面的 boot chain 是否有驗證 OP-TEE image
```

----------

## 9. Chain of Trust

Root of Trust 本身只是一個起點。實際系統要安全，必須建立完整的 Chain of Trust。

```
Root of Trust
  ↓
BootROM
  ↓
SPL / TF-A
  ↓
U-Boot
  ↓
Kernel
  ↓
RootFS
  ↓
Application / Service
```

每一層都要回答：

```
我是誰驗證的？
我又驗證了誰？
```

----------

## 10. Chain of Trust 中斷的常見位置

### 10.1 Bootloader 有驗證 Kernel，但沒有驗證 DTB

很多 BSP 只驗證 kernel image，卻忘記 DTB 也是攻擊面。

DTB 可以修改：

```
reserved-memory
kernel cmdline
device status
security related node
memory region
peripheral enable state
```

如果 DTB 沒有被保護，攻擊者可能不需要改 kernel code，只要改 DTB 就能改變系統行為。

----------

### 10.2 Kernel 有驗證，但 RootFS 沒驗證

如果 kernel 是可信的，但 rootfs 可以被修改，攻擊者仍然可以替換：

```
/init
system service
shared library
kernel module
configuration file
udev rule
systemd service
```

這種情況通常需要：

```
dm-verity
fs-verity
Android Verified Boot
signed package
read-only rootfs
```

----------

### 10.3 Firmware Update 沒有簽章驗證

即使 boot chain 是安全的，如果 update package 沒驗證，攻擊者仍然可以安裝惡意 firmware。

所以 firmware update 也必須納入 chain of trust：

```
Update package signature
  ↓
Version / rollback check
  ↓
Image verification
  ↓
Atomic update
  ↓
Boot verification
```

----------

### 10.4 沒有 Rollback Protection

如果攻擊者不能刷入惡意 image，他可能改成刷入舊版有漏洞的 image。

因此 Secure Boot 通常需要搭配：

```
Rollback Protection
Anti-rollback Counter
Security Version
RPMB counter
eFuse rollback index
AVB rollback index
```

----------

## 11. Root of Trust 與 Debug Interface

Root of Trust 不只跟 boot image 有關，也跟 debug interface 有關。

常見 debug interface：

```
JTAG
SWD
UART download mode
USB recovery mode
fastboot
U-Boot shell
kernel debugfs
serial console
```

在開發板階段，這些功能很方便。

但在量產階段，它們可能是攻擊入口。

----------

### 11.1 常見風險

```
JTAG 可讀取 memory
U-Boot shell 可改 bootargs
fastboot 可刷 unsigned image
UART download mode 可繞過 normal boot
debugfs 暴露 kernel internal state
```

----------

### 11.2 Production Lock

量產時常見做法：

```
disable JTAG
lock bootloader
disable unsigned fastboot flash
disable UART download mode
remove debug command
disable insecure kernel config
enable SELinux enforcing
enable verified boot
```

----------

## 12. Root of Trust 與 Device Lifecycle

很多 SoC 或安全架構會把裝置分成不同生命週期：

```
Development
Provisioning
Production
RMA
End-of-life
```

不同階段允許不同能力。

----------

### 12.1 Development

```
Secure Boot may be disabled
Debug port enabled
Unsigned image allowed
Keys not provisioned
Rollback protection disabled
```

適合 bring-up，但不適合出貨。

----------

### 12.2 Provisioning

```
Burn eFuse / OTP
Install device key
Install product key hash
Set rollback counter initial value
Configure secure storage
```

這是最危險的階段之一，因為燒錯 key 或 fuse 可能導致 device brick。

----------

### 12.3 Production

```
Secure Boot enabled
Debug locked
Only signed image allowed
Rollback protection enabled
RootFS verification enabled
```

這是量產出貨狀態。

----------

### 12.4 RMA

RMA 需要允許維修，但又不能破壞安全模型。

常見做法：

```
RMA token
challenge-response unlock
limited debug
audit log
temporary unlock
```

----------

## 13. Root of Trust 與 Embedded Linux / Android 的關係

### 13.1 Embedded Linux

Embedded Linux 常見安全鏈：

```
BootROM
  ↓
TF-A / SPL
  ↓
U-Boot verified boot
  ↓
FIT image signature
  ↓
Kernel + DTB + initramfs
  ↓
dm-verity / signed rootfs
  ↓
application update verification
```

常見技術：

```
U-Boot FIT signature
Yocto image signing
dm-verity
fs-verity
IMA / EVM
TPM measured boot
OP-TEE secure storage
RPMB
```

----------

### 13.2 Android

Android 常見安全鏈：

```
BootROM
  ↓
Bootloader
  ↓
AVB
  ↓
vbmeta
  ↓
boot / vendor_boot / dtbo / system / vendor
  ↓
dm-verity
  ↓
SELinux enforcing
```

Android 會特別關注：

```
AVB
vbmeta chain
rollback index
device lock state
verified boot state
key provisioning
TEE / KeyMint / Gatekeeper
RPMB
```

----------

## 14. BSP Debug 時應該問的問題

在分析一個平台是否具備 Root of Trust 時，可以依照以下順序檢查。

----------

### 14.1 BootROM / SoC

```
SoC 是否支援 Secure Boot？
BootROM 是否能驗證第一階段 bootloader？
驗證使用 RSA / ECDSA / Ed25519？
key hash 存在哪裡？
Secure Boot enable bit 是否在 eFuse？
是否有 boot lifecycle state？
```

----------

### 14.2 Bootloader

```
SPL / TF-A 是否被 BootROM 驗證？
U-Boot 是否被 SPL / TF-A 驗證？
U-Boot 是否驗證 kernel？
DTB 是否一起被驗證？
initramfs 是否一起被驗證？
```

----------

### 14.3 Kernel / RootFS

```
rootfs 是否 read-only？
是否使用 dm-verity？
kernel module 是否要求簽章？
initramfs 是否可被替換？
kernel cmdline 是否可信？
```

----------

### 14.4 Firmware Update

```
update package 是否簽章？
是否檢查版本？
是否支援 rollback protection？
是否使用 A/B update？
更新失敗是否能安全回復？
```

----------

### 14.5 Key / Fuse

```
key 是誰產生的？
private key 存在哪裡？
public key hash 是否燒入 eFuse？
是否有測試 key 與 production key 區分？
是否能 revoke 舊 key？
```

----------

## 15. Root of Trust 常見錯誤觀念

### 15.1 只要開 Secure Boot 就安全

不一定。

如果只有 BootROM 驗證 SPL，但後面 U-Boot 沒有驗證 kernel，安全鏈仍然中斷。

```
Secure Boot must protect the whole chain.
```

----------

### 15.2 Kernel 有簽章就夠了

不夠。

DTB、initramfs、rootfs、firmware update package 都可能成為攻擊面。

----------

### 15.3 OP-TEE 可以解決所有安全問題

不行。

OP-TEE 是 secure world runtime，但如果 boot chain 沒有驗證 OP-TEE image，攻擊者仍可能替換 secure world image。

----------

### 15.4 eFuse 可以隨便燒

不行。

eFuse / OTP 通常不可逆。

錯誤範例：

```
燒錯 public key hash
提前 lock debug port
燒錯 rollback index
使用測試 key 進入 production
```

這些都可能造成 device 無法開機或無法維修。

----------

## 16. Engineering View：Root of Trust 檢查表

```
[ ] SoC BootROM supports secure boot
[ ] Secure boot enable bit is documented
[ ] Public key hash location is known
[ ] eFuse / OTP programming flow is controlled
[ ] SPL / TF-A is signed
[ ] U-Boot is signed
[ ] Kernel image is signed
[ ] DTB is signed or included in signed image
[ ] initramfs is signed if used
[ ] RootFS verification is enabled
[ ] Firmware update package is signed
[ ] Rollback protection is enabled
[ ] Debug interface is locked in production
[ ] Test key and production key are separated
[ ] Recovery / RMA flow is defined
```

----------

## 17. 簡化架構圖

```
                  +-----------------------------+
                  |        Root of Trust        |
                  |  BootROM / eFuse / TPM / HUK|
                  +--------------+--------------+
                                 |
                                 v
                  +-----------------------------+
                  |       Chain of Trust        |
                  | Bootloader → Kernel → RootFS|
                  +--------------+--------------+
                                 |
              +------------------+------------------+
              |                                     |
              v                                     v
+-----------------------------+       +-----------------------------+
| Root of Trust for Verify    |       | Root of Trust for Measure   |
| Secure Boot                 |       | Measured Boot / TPM PCR     |
+-----------------------------+       +-----------------------------+
              |
              v
+-----------------------------+
| Root of Trust for Storage   |
| Key / RPMB / Secure Storage |
+-----------------------------+
```

## 18. 名詞速查表

| 名詞 | 全名 / 中文 | 工程上可以怎麼理解 | 常見位置 / 用途 |
|------|-------------|------------------|----------------|
| Root of Trust | 信任根 | 整個系統安全信任鏈的起點 | BootROM、eFuse、TPM、Secure Element |
| Chain of Trust | 信任鏈 | 前一階段驗證下一階段，形成連續信任關係 | BootROM → Bootloader → Kernel → RootFS |
| BootROM | Boot Read-Only Memory | SoC 內部不可修改的第一段開機程式 | 上電後最先執行，載入並驗證第一階段 bootloader |
| Secure Boot | 安全開機 | 不可信的 image 不允許執行 | BootROM / Bootloader 驗證簽章 |
| Trusted Boot | 可信開機 | 系統啟動過程被記錄，可供後續判斷是否可信 | 常與 TPM / Measured Boot 搭配 |
| Measured Boot | 量測開機 | 不一定阻擋開機，而是記錄載入內容的 hash | TPM PCR、attestation |
| Verification | 驗證 | 檢查 image 簽章是否正確 | Secure Boot、AVB、FIT signature |
| Measurement | 量測 | 記錄 image / config 的 hash | TPM PCR、Measured Boot |
| Attestation | 遠端證明 / 平台證明 | 向本地或遠端證明目前系統狀態可信 | TPM quote、TEE attestation |
| eFuse | Electronic Fuse | SoC 內一次性燒錄設定 | Secure Boot enable、key hash、rollback index |
| OTP | One-Time Programmable Memory | 一次性可寫入記憶體 | 類似 eFuse，用於保存不可逆安全設定 |
| Public Key Hash | 公鑰雜湊 | 不直接存完整公鑰，而是存公鑰 hash 驗證可信性 | eFuse / OTP |
| Private Key | 私鑰 | 用來簽署 image，絕對不能放在 device | Build server、HSM、signing machine |
| Public Key | 公鑰 | 驗證 image 簽章 | image header、bootloader、vbmeta |
| HUK | Hardware Unique Key | 每顆晶片唯一硬體金鑰 | key derivation、secure storage |
| Secure Storage | 安全儲存 | 受硬體或 TEE 保護的儲存空間 | key、counter、憑證、device secret |
| RPMB | Replay Protected Memory Block | eMMC/UFS 安全區域 | rollback counter、TEE secure storage |
| TPM | Trusted Platform Module | 獨立安全晶片 / firmware 安全模組 | measured boot、PCR、attestation |
| PCR | Platform Configuration Register | TPM 裡記錄 boot measurement | firmware、kernel hash |
| TEE | Trusted Execution Environment | 與 normal world 隔離的可信執行環境 | OP-TEE、KeyMint |
| ARM TrustZone | ARM 硬體隔離技術 | secure world / normal world 隔離 | secure monitor、secure peripheral |
| OP-TEE | Open Portable TEE | 開源 TEE OS | secure storage、TA |
| TA | Trusted Application | TEE 裡執行的可信應用 | crypto、key management |
| Normal World | 一般世界 | Linux / Android 運行環境 | Kernel、userspace |
| Secure World | 安全世界 | TrustZone 隔離出的環境 | OP-TEE |
| TF-A | Trusted Firmware-A | ARM secure firmware | BL1 / BL2 / BL31 |
| SPL | Secondary Program Loader | U-Boot 前期載入器 | DRAM init |
| U-Boot | Universal Bootloader | Embedded Linux bootloader | 載入 kernel / DTB |
| FIT Image | Flattened Image Tree | U-Boot image 打包格式 | kernel、DTB、signature |
| DTB | Device Tree Blob | 硬體描述資料 | kernel boot |
| initramfs | Initial RAM Filesystem | early rootfs | early userspace |
| dm-verity | Device Mapper Verity | block device 完整性驗證 | rootfs verification |
| fs-verity | File-system Verity | 單檔案完整性驗證 | APK、firmware |
| AVB | Android Verified Boot | Android verified boot 架構 | vbmeta |
| vbmeta | Verified Boot Metadata | AVB metadata | hash、rollback index |
| Rollback Protection | 防降版保護 | 防止刷回舊漏洞版本 | eFuse、RPMB |
| Anti-rollback Counter | 防降版計數器 | 記錄最低允許版本 | secure storage |
| Device Lifecycle | 裝置生命週期 | 安全狀態管理 | Open / Production / RMA |
| Provisioning | 安全資料佈署 | 寫入 key、fuse | 工廠 |
| RMA | 維修模式 | 特殊受控模式 | debug unlock |
| JTAG / SWD | 硬體除錯介面 | CPU debug | 量產需關閉 |
| Fastboot | Android 刷機協定 | image flash | bootloader |
| Recovery Mode | 恢復模式 | 系統修復更新 | update package |
| Signing Key | 簽章金鑰 | 簽署 image | build infrastructure |
| Key Revocation | 金鑰撤銷 | 停用舊 key | certificate chain |
