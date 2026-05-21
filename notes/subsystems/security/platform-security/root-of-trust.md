
# 🧠 Root of Trust

## 🎯 本章目的

本章要先建立 Platform Security 最核心的概念：

```
Root of Trust
Chain of Trust
Secure Boot
Measured Boot
Secure Storage
```

重點不是一次講完所有安全技術，而是先理解：

```
系統從哪裡開始被信任？
誰驗證誰？
key / counter / secure storage 為什麼不能只放在 rootfs？
```

----------

## 🧭 一張圖先看懂

```
[Power On]
    ↓
[BootROM]
    ↓
[eFuse / OTP key hash]
    ↓
[Verify first bootloader]
    ↓
[SPL / TF-A]
    ↓
[Verify U-Boot]
    ↓
[U-Boot]
    ↓
[Verify Kernel / DTB / initramfs]
    ↓
[Linux / Android]
    ↓
[Verify RootFS / System partition]
```

一句話：

```
Root of Trust 是信任鏈的起點。
Chain of Trust 是一層驗證下一層。
```

----------

## 1️⃣ Root of Trust 是什麼？

Root of Trust 可以理解成：

```
系統中最早被信任，而且一般軟體無法修改的安全起點。
```

在 ARM SoC / Embedded Linux / Android 平台中，Root of Trust 常見來源是：

```
BootROM
eFuse / OTP
Hardware Unique Key
TPM
Secure Element
TEE / TrustZone secure world
```

但對 BSP 工程師來說，最常遇到的是：

```
BootROM + eFuse / OTP
```

也就是：

```
BootROM 內建驗證邏輯
eFuse / OTP 保存 public key hash 或 secure boot 狀態
```

----------


## 2️⃣ 名詞速查表

| 名詞 | 中文理解 | BSP 工程上可以怎麼理解 |
|------|----------|------------------------|
| Root of Trust | 信任根 | 整個安全信任鏈的起點 |
| Chain of Trust | 信任鏈 | 前一階段驗證下一階段 |
| BootROM | 開機 ROM | SoC 上電後最先執行、通常不可修改 |
| eFuse / OTP | 一次性燒錄區 | 保存 key hash、secure boot enable、rollback index |
| Secure Boot | 安全開機 | 驗證失敗就不執行 |
| Measured Boot | 量測開機 | 不一定阻擋，但記錄實際載入內容 |
| Secure Storage | 安全儲存 | 保存 key / counter / secret 的受保護儲存 |
| HUK | Hardware Unique Key | 每顆 SoC 獨有的硬體 key |
| TPM | Trusted Platform Module | 可做 measurement、attestation、key sealing |
| RPMB | Replay Protected Memory Block | 常用於 secure counter / secure storage |
| OP-TEE | Open Portable TEE | 跑在 TrustZone secure world 的 TEE OS |
| Rollback Protection | 防降版保護 | 防止刷回舊版有漏洞 image |

----------

## 3️⃣ Root of Trust 在 Boot Flow 的位置

一般 BSP boot flow 可能長這樣：

```
BootROM
  ↓
SPL / TF-A
  ↓
U-Boot
  ↓
Kernel + DTB
  ↓
RootFS
```

Security 視角會變成：

```
BootROM
  ↓ verify
SPL / TF-A
  ↓ verify
U-Boot
  ↓ verify
Kernel + DTB + initramfs
  ↓ verify
RootFS / Android partitions
```

差異在於：

```
一般 boot flow:
  重點是能不能開機

security boot flow:
  重點是每一層是不是可信
```

----------

## 4️⃣ Root of Trust 的三種角色

Root of Trust 通常可以分成三類。

```
Root of Trust for Verification
Root of Trust for Measurement
Root of Trust for Storage
```

----------

### 4.1 Verification：負責驗證

用途：

```
確認下一階段 image 是不是被授權的 image。
```

典型例子：

```
BootROM 使用 eFuse 裡的 key hash
驗證 first bootloader 的 signature
```

流程：

```
[BootROM]
    ↓
[Read key hash from eFuse]
    ↓
[Verify SPL / TF-A signature]
    ↓
[Pass control to SPL / TF-A]
```

如果驗證失敗：

```
停止開機
進入 recovery mode
進入 USB download mode
直接報錯
```

依 SoC 設計而定。

----------

### 4.2 Measurement：負責記錄

用途：

```
記錄系統實際載入了什麼。
```

典型例子：

```
Firmware / bootloader 把 kernel hash extend 到 TPM PCR。
```

流程：

```
[Bootloader]
    ↓
[Measure kernel / cmdline / initramfs]
    ↓
[Extend hash to TPM PCR]
    ↓
[Continue boot]
```

重點：

```
Measured Boot 不一定會擋開機。
它主要是留下紀錄，讓後續可以做 attestation。
```

----------

### 4.3 Storage：負責保護資料

用途：

```
保存不能被 Normal World 隨便讀寫的資料。
```

常見資料：

```
device key
disk encryption key
rollback counter
RPMB key
attestation key
secure storage metadata
```

常見儲存位置：

```
eFuse / OTP
TPM NVRAM
RPMB
TEE secure storage
Secure Element
```

重點：

```
key / counter / secret 不應該只放在普通 rootfs。
```

----------

## 5️⃣ Chain of Trust 怎麼建立？

Root of Trust 只是起點。

真正的系統安全需要建立完整的 Chain of Trust。

```
Root of Trust
  ↓
BootROM
  ↓
SPL / TF-A
  ↓
U-Boot
  ↓
Kernel / DTB / initramfs
  ↓
RootFS / Android partitions
```

核心規則：

```
前一層驗證下一層。
```

也就是：

```
BootROM 驗證 SPL / TF-A
SPL / TF-A 驗證 U-Boot
U-Boot 驗證 Kernel / DTB / initramfs
Kernel 驗證 RootFS
Update system 驗證 firmware package
```

----------

## 6️⃣ BootROM + eFuse 的典型 Secure Boot Flow

這是 BSP 最常遇到的模型。

```
[Power On]
    ↓
[BootROM starts]
    ↓
[Read secure boot enable bit]
    ↓
[Read public key hash from eFuse / OTP]
    ↓
[Load first bootloader from boot media]
    ↓
[Check image signature]
    ↓
[Compare public key hash]
    ↓
[Signature OK?]
    ↓
[Jump to first bootloader]
```

如果 image 被修改：

```
signature mismatch
```

如果使用錯誤 key 簽章：

```
public key hash mismatch
```

如果 secure boot fuse 已經 enable：

```
unsigned image 不應該再被允許執行
```

----------

## 7️⃣ 為什麼 DTB / initramfs 也要保護？

很多人只注意 kernel image，但在 BSP 裡：

```
DTB
initramfs
kernel cmdline
```

也都可能影響系統安全。

----------

### DTB 可以改變什麼？

DTB 可以影響：

```
reserved-memory
device status
interrupt routing
memory region
secure / non-secure device exposure
kernel bootargs
```

如果 kernel 有簽，但 DTB 沒簽：

```
攻擊者可能不改 kernel code，只改 DTB 就改變系統行為。
```

----------

### initramfs 可以改變什麼？

initramfs 可能包含：

```
early userspace
mount script
rootfs unlock logic
dm-verity setup
firmware loading logic
```

如果 initramfs 沒有被驗證：

```
攻擊者可能在真正 rootfs mount 前就介入開機流程。
```

----------

## 8️⃣ Root of Trust 常見中斷點

### ❌ 只驗證 bootloader，沒驗證 kernel

```
BootROM → SPL verified
SPL → U-Boot loaded
U-Boot → Kernel not verified
```

結果：

```
攻擊者仍可替換 kernel。
```

----------

### ❌ kernel 有簽，但 DTB 沒簽

```
U-Boot verifies Image
U-Boot loads unsigned board.dtb
```

結果：

```
攻擊者可以透過 DTB 改 bootargs / reserved-memory / device status。
```

----------

### ❌ rootfs 沒有驗證

```
Kernel is trusted
RootFS is writable ext4
```

結果：

```
/sbin/init
systemd service
shared library
application
```

仍可能被替換。

----------

### ❌ update package 沒有驗證

```
Normal boot path is secure
Firmware update path accepts unsigned package
```

結果：

```
攻擊者可以透過 update path 寫入惡意 image。
```

----------

### ❌ 沒有 rollback protection

```
v2 修掉安全漏洞
v1 仍然是合法簽章 image
device 允許刷回 v1
```

結果：

```
攻擊者可以刷回舊版漏洞 image。
```

----------

## 9️⃣ BSP Debug / Review 重點

### Step 1：確認 Root of Trust 在哪裡

先問：

```
這顆 SoC 的第一個信任點是什麼？
BootROM 是否支援 secure boot？
secure boot enable 狀態存在哪裡？
public key hash 存在哪裡？
```

常見答案：

```
BootROM
eFuse / OTP
SoC vendor secure boot mechanism
```

----------

### Step 2：確認誰驗證誰

畫出自己的平台 flow：

```
BootROM
  ↓
SPL / TF-A
  ↓
U-Boot
  ↓
Kernel / DTB / initramfs
  ↓
RootFS
```

然後逐層標記：

```
這一層是否有 signature verification？
驗證用的 key 來自哪裡？
下一層 image 是否完整包含在驗證範圍？
```

----------

### Step 3：確認 key / fuse 狀態

檢查：

```
使用 test key 還是 production key？
public key hash 是否已經 provision？
secure boot fuse 是否已經 enable？
debug fuse 是否被 lock？
rollback counter 是否已經初始化？
```

注意：

```
eFuse / OTP 通常不可逆。
```

所以 production 前一定要確認流程。

----------

### Step 4：確認 rootfs / partition 保護

Embedded Linux 常見：

```
dm-verity
read-only rootfs
signed update package
FIT signature
IMA / EVM
```

Android 常見：

```
AVB
vbmeta
dm-verity
rollback index
locked bootloader
```

----------

## 🔟 常見錯誤觀念

### ❌ Secure Boot enable 就代表整台機器安全

不一定。

如果只驗證第一階段 bootloader，後面 kernel / rootfs 沒驗證，信任鏈還是中斷。

----------

### ❌ Kernel signed 就夠了

不夠。

至少還要確認：

```
DTB 是否驗證
initramfs 是否驗證
rootfs 是否驗證
update package 是否驗證
```

----------

### ❌ key 可以放在 rootfs

不建議。

rootfs 是 Normal World 可見的儲存空間。

敏感資料應該使用：

```
TEE secure storage
TPM
RPMB
Secure Element
eFuse / OTP
```

----------

### ❌ eFuse 可以先燒再說

非常危險。

常見不可逆錯誤：

```
燒錯 public key hash
提前 enable secure boot
提前 lock debug port
rollback index 設錯
test key 被燒成 production key
```
----------

## 1️⃣1️⃣ BSP Checklist

```
[ ] SoC BootROM secure boot capability is known
[ ] Root of Trust location is identified
[ ] Secure boot enable bit location is known
[ ] Public key hash location is known
[ ] SPL / TF-A verification is defined
[ ] U-Boot verification is defined
[ ] Kernel verification is defined
[ ] DTB is included in verification scope
[ ] initramfs is included in verification scope if used
[ ] RootFS / partition verification is defined
[ ] Firmware update package verification is defined
[ ] Rollback protection is defined
[ ] Debug interface policy is defined
[ ] Test key and production key are separated
[ ] eFuse / OTP provisioning flow is reviewed
```
