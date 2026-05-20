
# 🧠 Secure Boot vs Trusted Boot

本章節整理兩個很容易混淆的概念：

```
Secure Boot
Trusted Boot
```

它們都跟「開機是否可信」有關，但核心目的不同。

簡單來說：

```
Secure Boot:
  不可信的東西，不讓它執行。

Trusted Boot:
  系統實際執行了什麼，把它記錄下來，之後再判斷是否可信。
```

----------

## 1. 為什麼要區分 Secure Boot 與 Trusted Boot？

在 BSP / Embedded Linux / Android 平台開發時，常會看到這些名詞：

```
Secure Boot
Verified Boot
Trusted Boot
Measured Boot
Attestation
dm-verity
AVB
TPM PCR
```

很多時候大家會把它們混在一起講：

```
這台機器有開 Secure Boot，所以它是 trusted。
```

但這句話其實不夠精準。

因為：

```
Secure Boot 關心的是：
  image 是否被允許執行？

Trusted Boot / Measured Boot 關心的是：
  實際執行了哪些 image？
  系統狀態是否可以被證明？
```

兩者可以搭配使用，但不是同一件事。

----------


## 2. 名詞速查表

| 名詞 | 中文理解 | 核心目的 | 常見技術 |
|------|----------|----------|----------|
| Secure Boot | 安全開機 | 驗證 image，不可信就不執行 | BootROM signature check、U-Boot FIT signature |
| Verified Boot | 驗證開機 | 確保 boot / system image 未被竄改 | Android AVB、dm-verity |
| Trusted Boot | 可信開機 | 建立可被信任的系統狀態證據 | TPM、PCR、event log |
| Measured Boot | 量測開機 | 把啟動過程的 hash 記錄起來 | TPM PCR extend |
| Attestation | 平台證明 | 向本地或遠端證明目前狀態 | TPM quote、TEE attestation |
| Chain of Trust | 信任鏈 | 前一階段驗證或量測下一階段 | BootROM → Bootloader → Kernel |
| Root of Trust | 信任根 | 信任鏈起點 | BootROM、eFuse、TPM、Secure Element |
| PCR | Platform Configuration Register | TPM 裡保存 measurement 結果 | PCR0、PCR1、PCR7 |
| Event Log | 事件紀錄 | 記錄每次 measurement 的內容 | firmware、bootloader、kernel hash |
| dm-verity | block device 完整性驗證 | 防止 rootfs 被修改 | Linux rootfs verification |
| AVB | Android Verified Boot | Android 的 verified boot 架構 | vbmeta、rollback index |
----------

## 3. Secure Boot 是什麼？

Secure Boot 的核心概念是：

```
只允許被授權的程式碼執行。
```

也就是：

```
image 有合法簽章 → 可以執行
image 沒有合法簽章 → 停止開機
```

----------

## 4. Secure Boot 的基本流程

典型 Secure Boot flow：

```
+-----------------------------+
| SoC BootROM                 |
| Immutable code              |
+-----------------------------+
              │
              │ verify signature
              ▼
+-----------------------------+
| SPL / TF-A BL2              |
+-----------------------------+
              │
              │ verify signature
              ▼
+-----------------------------+
| U-Boot / BL33               |
+-----------------------------+
              │
              │ verify signature
              ▼
+-----------------------------+
| Kernel + DTB + initramfs    |
+-----------------------------+
              │
              │ verify rootfs
              ▼
+-----------------------------+
| RootFS / Android System     |
+-----------------------------+
```

Secure Boot 的重點是：

```
每一層在執行下一層之前，先驗證下一層。
```

----------

## 5. Secure Boot 的工程意義

對 BSP 工程師來說，Secure Boot 代表：

```
不是只要 image 能 boot 就好。
還要確認 image 是誰簽的、用哪把 key 驗證、驗證範圍包含哪些內容。
```

常見檢查點：

```
BootROM 是否驗證 SPL / TF-A？
SPL 是否驗證 U-Boot？
U-Boot 是否驗證 kernel？
DTB 是否包含在簽章範圍內？
initramfs 是否包含在簽章範圍內？
rootfs 是否有 dm-verity / AVB？
recovery image 是否也有驗證？
```

----------

## 6. Secure Boot 可以防什麼？

Secure Boot 可以防止：

```
替換 bootloader
替換 kernel
替換 DTB
替換 initramfs
刷入 unsigned firmware
修改 Android boot image
修改 system / vendor partition
```

前提是：

```
這些 image 都有被納入驗證鏈。
```

----------

## 7. Secure Boot 不能防什麼？

Secure Boot 不是萬能的。

它通常不能直接防止：

```
已簽章 image 內的漏洞
runtime exploit
合法版本中的 CVE
已授權但有 bug 的 kernel module
userspace service vulnerability
已解鎖 debug interface
被允許的舊版 image
```

所以 Secure Boot 通常還需要搭配：

```
rollback protection
kernel hardening
SELinux
dm-verity
IMA / EVM
TPM measured boot
runtime integrity check
secure firmware update
```

----------

## 8. Trusted Boot 是什麼？

Trusted Boot 的核心概念是：

```
記錄系統實際載入了什麼，讓後續可以判斷是否可信。
```

它不一定會阻止系統開機。

比較像是：

```
我不一定馬上擋你，但我會把你開機過程中載入的東西全部記錄下來。
```

----------

## 9. Trusted Boot 與 Measured Boot 的關係

很多情境下，Trusted Boot 會透過 Measured Boot 實作。

Measured Boot 會對開機過程中的元件做 hash：

```
firmware
bootloader
kernel
initramfs
kernel command line
device tree
configuration
```

然後把 hash extend 到 TPM PCR。

----------

## 10. Measured Boot 基本流程

```
+-----------------------------+
| Firmware / BootROM          |
+-----------------------------+
              │
              │ measure next stage
              ▼
+-----------------------------+
| TPM PCR                     |
| PCR = Extend(PCR, hash)     |
+-----------------------------+
              │
              │ load bootloader
              ▼
+-----------------------------+
| Bootloader                  |
+-----------------------------+
              │
              │ measure kernel / cmdline
              ▼
+-----------------------------+
| TPM PCR                     |
+-----------------------------+
              │
              │ boot kernel
              ▼
+-----------------------------+
| Linux                       |
+-----------------------------+
```

重點是：

```
TPM PCR 不是單純寫入 hash。
它是透過 extend 操作累積狀態。
```

概念上可以理解成：

```
PCR_new = Hash(PCR_old || new_measurement)
```

這樣可以保證 measurement 的順序也被納入結果。

----------

## 11. PCR 是什麼？

PCR 是：

```
Platform Configuration Register
```

它是 TPM 內部的 register，用來保存開機過程中的 measurement 結果。

可以把 PCR 想成：

```
系統開機狀態的摘要紀錄。
```

例如：

```
PCR0 可能記錄 firmware measurement
PCR1 可能記錄 platform config
PCR4 可能記錄 bootloader
PCR7 可能記錄 secure boot policy
```

實際 PCR 分配會依平台、firmware、OS、policy 而不同。

----------

## 12. Event Log 是什麼？

PCR 只保存最後的累積 hash。

但如果只有 PCR 值，工程師很難知道：

```
到底是哪個元件改變了？
```

所以 measured boot 通常會搭配 Event Log。

Event Log 會記錄：

```
誰被量測？
量測的 hash 是什麼？
量測順序是什麼？
extend 到哪個 PCR？
```

概念上：

```
PCR:
  最終摘要

Event Log:
  詳細紀錄
```

----------

## 13. Attestation 是什麼？

Attestation 是：

```
證明目前系統狀態可信的機制。
```

例如一台設備開機後，server 想知道：

```
這台 device 現在跑的是不是我們允許的 firmware？
kernel 有沒有被換掉？
bootloader policy 有沒有改？
```

這時 device 可以透過 TPM 或 TEE 提供 attestation。

----------

## 14. Remote Attestation 基本概念

```
+-----------------------------+
| Device                      |
| TPM / TEE                   |
+-----------------------------+
              │
              │ quote PCR / measurement
              ▼
+-----------------------------+
| Remote Server               |
| Compare with known-good     |
+-----------------------------+
              │
              ▼
+-----------------------------+
| Allow / Deny / Limited mode |
+-----------------------------+
```

如果 PCR 與 event log 符合預期，server 可以信任這台設備目前狀態。

如果不符合，server 可以拒絕提供服務或金鑰。

----------

## 15. Secure Boot vs Trusted Boot 核心差異


| 項目 | Secure Boot | Trusted Boot / Measured Boot |  
|------|-------------|------------------------------|  
| 核心目的 | 阻止不可信 image 執行 | 記錄實際載入內容 |  
| 行為 | 驗證失敗就停止開機 | 通常不一定阻止開機 |  
| 關鍵技術 | Signature verification | Hash measurement |  
| Root of Trust | BootROM / eFuse key hash | TPM / firmware measurement |  
| 結果 | 允許或拒絕執行 | 產生 PCR / event log |  
| 適合場景 | 防止 unsigned image | 證明系統狀態 |  
| 是否需要遠端驗證 | 不一定 | 常見 |  
| 常見搭配 | FIT signature、AVB | TPM quote、attestation |  
| 防 rollback | 需要額外機制 | 可記錄版本，但仍需 policy |  
| 對 BSP 的重點 | 驗證鏈是否完整 | measurement 是否完整且可解讀 |

----------

## 16. Secure Boot 與 Trusted Boot 可以一起用嗎？

可以，而且實務上常常一起使用。

```
Secure Boot:
  確保不可信 image 不會被執行。

Measured Boot:
  記錄實際執行了什麼。

Attestation:
  向外部證明目前狀態。
```

組合起來：

```
Secure Boot + Measured Boot + Attestation
```

可以形成更完整的平台安全模型。

----------

## 17. 組合架構圖

```
Power On
  ↓
Root of Trust
  ↓
Secure Boot verification
  ↓
Bootloader
  ↓
Measure bootloader / kernel / cmdline
  ↓
TPM PCR / Event Log
  ↓
Kernel
  ↓
dm-verity / AVB
  ↓
Runtime system
  ↓
Remote attestation
```

可以拆成兩條邏輯：

```
Verification path:
  image 是否允許執行？

Measurement path:
  實際執行了什麼？
```

----------

## 18. Embedded Linux 常見實作

在 Embedded Linux 中，Secure Boot 可能長這樣：

```
BootROM
  ↓ verify
SPL / TF-A
  ↓ verify
U-Boot
  ↓ verify FIT image
Kernel + DTB + initramfs
  ↓ verify
dm-verity rootfs
```

常見技術：

```
SoC secure boot
U-Boot verified boot
FIT image signature
signed kernel
signed DTB
signed initramfs
dm-verity
IMA / EVM
TPM measured boot
```

----------

## 19. U-Boot FIT Signature

U-Boot 常用 FIT Image 把多個 boot 元件包在一起：

```
kernel
DTB
initramfs
configuration
signature
```

簡化概念：

```
+-----------------------------+
| FIT Image                   |
|                             |
|  kernel                     |
|  dtb                        |
|  ramdisk                    |
|  config                     |
|  signature                  |
+-----------------------------+
```

優點是：

```
kernel、DTB、initramfs 可以一起被簽章保護。
```

這對 BSP 很重要，因為很多攻擊不是直接改 kernel，而是改 DTB 或 bootargs。

----------

## 20. Android 常見實作

Android 的 Verified Boot 通常由 AVB 實作。

```
Bootloader
  ↓ verify vbmeta
vbmeta
  ↓ verify descriptors
boot / vendor_boot / dtbo / system / vendor
  ↓
dm-verity
  ↓
Android userspace
```

常見元件：

```
AVB
vbmeta
rollback index
dm-verity
device lock state
orange / yellow / green boot state
KeyMint / Gatekeeper
RPMB
```

Android 的重點是：

```
不只 boot image 要驗證，
system / vendor / product / odm 等 partition 也要透過 vbmeta chain 納入驗證。
```

----------

## 21. Secure Boot 常見中斷點

### 21.1 只驗證 bootloader，沒驗證 kernel

```
BootROM verifies SPL
SPL loads U-Boot
U-Boot loads kernel without verification
```

問題：

```
攻擊者仍可替換 kernel。
```

----------

### 21.2 只驗證 kernel，沒驗證 DTB

```
U-Boot verifies Image
U-Boot loads unsigned board.dtb
```

問題：

```
攻擊者可以透過 DTB 改變硬體描述、reserved-memory、bootargs 或安全相關設定。
```

----------

### 21.3 rootfs 沒有完整性保護

```
Kernel is trusted
RootFS is writable and not verified
```

問題：

```
攻擊者可以替換 /sbin/init、systemd service、shared library 或應用程式。
```

----------

### 21.4 recovery / update path 沒驗證

```
Normal boot is verified
Recovery accepts unsigned update package
```

問題：

```
攻擊者可以透過 recovery 或 update 機制寫入惡意 image。
```

----------

### 21.5 沒有 rollback protection

```
New version has security fix
Old version is still signed
Device allows booting old version
```

問題：

```
攻擊者可以刷回舊版漏洞 image。
```

----------

## 22. Trusted Boot 常見問題

### 22.1 有 measurement，但沒有 policy

如果系統只是把 hash 記錄到 PCR，卻沒有人檢查，安全價值有限。

需要有 policy 判斷：

```
這組 PCR 是否允許？
這個 event log 是否符合 known-good？
是否允許釋放 disk key？
是否允許連接 backend service？
```

----------

### 22.2 Event Log 不完整

如果只量測 kernel，但沒有量測 kernel cmdline、DTB、initramfs，仍可能漏掉重要攻擊面。

應該確認 measurement 範圍包含：

```
bootloader
kernel
DTB
initramfs
kernel command line
security policy
critical config
```

----------

### 22.3 PCR 改變但無法解釋

BSP bring-up 時常見情況：

```
每次 boot PCR 都不一樣。
```

可能原因：

```
build timestamp
random seed
boot order change
firmware variable change
kernel cmdline change
device tree changed
event log ordering changed
```

這時不能只看 PCR，必須搭配 event log 分析。

----------

## 23. BSP Debug 檢查方向

### 23.1 Secure Boot Debug

```
確認 SoC secure boot enable 狀態
確認 eFuse / OTP key hash
確認 image 是否正確簽章
確認 bootloader 是否啟用 verification
確認 kernel / DTB / initramfs 是否在簽章範圍
確認 rootfs 是否有 dm-verity / AVB
確認 debug boot path 是否也受保護
```

----------

### 23.2 Trusted Boot Debug

```
確認 TPM 是否存在
確認 TPM driver 是否載入
確認 PCR 是否有變化
確認 event log 是否可讀
確認 measurement 範圍
確認 attestation quote 是否能產生
確認 known-good PCR baseline 是否建立
```

----------

## 24. Secure Boot Debug 常見 log 關鍵字

不同平台 log 不同，但常見關鍵字包含：

```
secure boot
hab
ahab
verified boot
avb
vbmeta
dm-verity
fit signature
rsa verify
ecdsa verify
authentication
image verification
rollback index
boot state
```

如果看到：

```
verification failed
bad signature
hash mismatch
rollback index error
vbmeta verification failed
dm-verity corruption
```

通常代表驗證鏈某一層失敗。

----------

## 25. Trusted Boot Debug 常見檔案與指令

Linux 上常見檢查方向：

```
/sys/kernel/security/tpm0/binary_bios_measurements
/sys/class/tpm/
dmesg | grep -i tpm
```

常見工具：

```
tpm2_pcrread
tpm2_eventlog
tpm2_quote
tpm2_getcap
```

概念上可檢查：

```
TPM 是否存在
PCR 是否有值
event log 是否存在
measurement 是否符合預期
```

----------

## 26. 工程案例：只有 Secure Boot，沒有 RootFS Verification

假設平台如下：

```
BootROM verifies SPL
SPL verifies U-Boot
U-Boot verifies kernel
Kernel mounts ext4 rootfs
RootFS is writable
```

表面上：

```
Secure Boot enabled
```

但實際上：

```
RootFS 沒有被保護。
```

攻擊者如果能修改 rootfs，就可以替換：

```
/etc/init.d/*
systemd service
shared library
application binary
configuration file
```

改善方向：

```
read-only rootfs
dm-verity
signed update package
A/B update
factory reset protection
```

----------

## 27. 工程案例：有 Measured Boot，但沒有 Attestation

假設平台如下：

```
Bootloader measures kernel
Kernel measures initramfs
TPM PCR has values
```

但沒有：

```
local policy
remote server
key sealing
known-good baseline
```

那 measurement 只是被記錄，沒有人使用。

改善方向：

```
建立 known-good PCR baseline
使用 TPM quote 做 attestation
把 disk key sealed 到特定 PCR
backend service 檢查 device state
```

----------

## 28. 工程案例：Android AVB 已開，但 Bootloader Unlock

Android 平台可能出現：

```
AVB enabled
vbmeta valid
dm-verity enabled
```

但 bootloader 是 unlocked。

這代表：

```
使用者或攻擊者可能允許刷入自訂 image。
```

Android 會透過 boot state 表示：

```
green:
  locked，使用 trusted key，verified boot 通過

yellow:
  locked，但使用 custom key

orange:
  unlocked，verification 不完整或允許自訂 image

red:
  verification failed
```

BSP / product 階段要確認：

```
production device 是否應該 locked
unlock 是否會清除 user data
fastboot flash 是否限制 unsigned image
rollback index 是否仍然生效
```

----------

## 29. Secure Boot 與 Trusted Boot 的關係總圖

```
                         +-----------------------------+
                         |        Root of Trust        |
                         +--------------+--------------+
                                        |
              +-------------------------+-------------------------+
              |                                                   |
              v                                                   v
+-----------------------------+                 +-----------------------------+
| Secure Boot                 |                 | Trusted / Measured Boot     |
|                             |                 |                             |
| Verify before execute       |                 | Measure what was executed   |
|                             |                 |                             |
| Invalid image -> stop       |                 | Record hash -> PCR/log      |
+-------------+---------------+                 +--------------+--------------+
              |                                                  |
              v                                                  v
+-----------------------------+                 +-----------------------------+
| Chain of Trust              |                 | Attestation                 |
| BootROM → Kernel → RootFS   |                 | Prove current device state  |
+-----------------------------+                 +-----------------------------+
```

----------

## 30. BSP Checklist

### 30.1 Secure Boot Checklist

```
[ ] SoC BootROM supports secure boot
[ ] Secure boot enable state is known
[ ] Public key hash is provisioned
[ ] SPL / TF-A is signed
[ ] U-Boot is signed
[ ] Kernel image is signed
[ ] DTB is signed or included in signed FIT / boot image
[ ] initramfs is signed if used
[ ] rootfs verification is enabled
[ ] recovery / update path is verified
[ ] rollback protection is enabled
[ ] debug boot mode is restricted
[ ] production bootloader is locked
```

----------

### 30.2 Trusted Boot / Measured Boot Checklist

```
[ ] TPM / fTPM / TEE measurement backend exists
[ ] PCR values can be read
[ ] Event log can be read
[ ] Firmware is measured
[ ] Bootloader is measured
[ ] Kernel is measured
[ ] DTB is measured
[ ] Kernel command line is measured
[ ] initramfs is measured
[ ] Known-good baseline is defined
[ ] Attestation flow is defined
[ ] Key sealing policy is defined if needed
```
