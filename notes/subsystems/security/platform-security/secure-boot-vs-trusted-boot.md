
# 🧠 Secure Boot vs Trusted Boot

## 🎯 本章目的

本章要釐清幾個容易混在一起的名詞：

```
Secure Boot
Verified Boot
Trusted Boot
Measured Boot
Attestation
```

它們都跟「開機是否可信」有關，但重點不同。

本章核心問題是：

```
誰負責阻止不可信 image？
誰負責記錄實際載入內容？
誰負責向外證明目前系統狀態？
```

----------

## 🧭 一張圖先看懂

```
                 [Root of Trust]
                        ↓
        +---------------+---------------+
        |                               |
        v                               v
[Secure Boot / Verified Boot]   [Trusted / Measured Boot]
        |                               |
        | verify signature / hash       | measure hash
        |                               |
        v                               v
[Allow or stop boot]             [TPM PCR / Event Log]
                                        |
                                        v
                                  [Attestation]
```

一句話：

```
Secure Boot 負責擋下不可信 image。
Measured Boot 負責記錄實際載入內容。
Attestation 負責證明目前系統狀態。
```

----------


## 1️⃣ 名詞速查表

| 名詞 | 中文理解 | 核心目的 | 常見技術 |
|------|----------|----------|----------|
| Secure Boot | 安全開機 | 不可信 image 不執行 | BootROM signature check、FIT signature |
| Verified Boot | 驗證開機 | 確認 boot / system image 沒被竄改 | Android AVB、dm-verity |
| Trusted Boot | 可信開機 | 建立可被信任的開機狀態 | Measured Boot、TPM |
| Measured Boot | 量測開機 | 記錄實際載入內容的 hash | TPM PCR extend、event log |
| Attestation | 平台證明 | 證明目前系統狀態 | TPM quote、TEE attestation |
| PCR | Platform Configuration Register | 保存 measurement 結果 | TPM PCR |
| Event Log | 事件紀錄 | 解釋 PCR 是由哪些 measurement 組成 | TPM event log |

----------

## 2️⃣ 最大差異：Verify vs Measure


## 📌 最重要的差異

| 項目 | Secure Boot / Verified Boot | Trusted Boot / Measured Boot |
|------|-----------------------------|------------------------------|
| 核心動作 | Verify | Measure |
| 問題 | 這個 image 可不可以執行？ | 實際執行了什麼？ |
| 失敗時 | 通常停止開機 | 通常仍可繼續開機 |
| 產物 | pass / fail | PCR / event log |
| 常見用途 | 防止 unsigned / modified image | attestation、key sealing、audit |
| 工程重點 | 驗證鏈是否完整 | measurement 是否完整且可解讀 |

簡化理解：

```
Secure Boot:
  不可信 → 不執行

Measured Boot:
  有載入 → 就記錄
```

----------

## 3️⃣ Secure Boot 是什麼？

Secure Boot 的核心概念是：

```
在執行下一階段 image 之前，先驗證它是不是被授權。
```

典型 flow：

```
[BootROM]
    ↓ verify
[SPL / TF-A]
    ↓ verify
[U-Boot]
    ↓ verify
[Kernel / DTB / initramfs]
    ↓ verify
[RootFS / System partition]
```

如果驗證失敗：

```
不跳轉執行
停止開機
進入 recovery
進入 download mode
```

依平台設計而定。

----------

## 4️⃣ Secure Boot 保護什麼？

Secure Boot / Verified Boot 可以保護：

```
bootloader
kernel image
DTB
initramfs
rootfs
Android boot / vendor_boot / dtbo / system / vendor partition
firmware update package
```

但前提是：

```
這些內容都有被納入驗證範圍。
```

如果某個 image 沒有被驗證，它就是信任鏈的缺口。

----------

## 5️⃣ Secure Boot 不能解決什麼？

Secure Boot 不代表系統沒有漏洞。

它不能直接解決：

```
合法簽章 image 裡面的 CVE
runtime exploit
userspace service vulnerability
kernel driver bug
已簽章但過舊的 image
production device 上仍開著的 debug interface
```

所以 Secure Boot 通常還要搭配：

```
rollback protection
dm-verity
SELinux enforcing
kernel hardening
firmware update verification
debug lock
```

----------

## 6️⃣ Verified Boot 是什麼？

Verified Boot 通常可以理解成：

```
Secure Boot 在 OS / partition 層級的延伸。
```

例如 Android AVB：

```
[Bootloader]
    ↓ verify
[vbmeta]
    ↓ verify descriptors
[boot / vendor_boot / dtbo / system / vendor]
    ↓
[dm-verity]
    ↓
[Android userspace]
```

Embedded Linux 也可以有類似概念：

```
[U-Boot]
    ↓ verify FIT image
[Kernel + DTB + initramfs]
    ↓
[dm-verity rootfs]
```

重點：

```
不是只有 bootloader 要可信。
OS image / rootfs / partition 也要可信。
```

----------

## 7️⃣ Trusted Boot / Measured Boot 是什麼？

Trusted Boot 常常透過 Measured Boot 實作。

Measured Boot 的核心概念是：

```
不要只問 image 是否允許執行。還要記錄實際載入了什麼。
```

典型 flow：

```
[Bootloader]
    ↓ measure
[Kernel hash]
    ↓ extend
[TPM PCR]

[Bootloader]
    ↓ measure
[Kernel cmdline hash]
    ↓ extend
[TPM PCR]

[Bootloader]
    ↓ measure
[initramfs hash]
    ↓ extend
[TPM PCR]
```

重點：

```
Measured Boot 不一定阻止開機。它主要產生可被檢查的紀錄。
```

----------

## 8️⃣ PCR 是什麼？

PCR 是：

```
Platform Configuration Register
```

它是 TPM 裡用來保存 measurement 結果的 register。

PCR 不是單純覆蓋寫入，而是 extend：

```
PCR_new = Hash(PCR_old || new_measurement)
```

這代表：

```
載入內容會影響 PCR載入順序也會影響 PCR
```

所以 PCR 可以用來代表：

```
目前系統開機狀態的摘要
```

----------

## 9️⃣ Event Log 是什麼？

PCR 只是一組最後結果。

但只有 PCR 不容易知道是哪裡變了。

所以 Measured Boot 會搭配 Event Log。

```
PCR:
  最終累積 hash

Event Log:
  每次量測的詳細紀錄
```

Event Log 通常會記錄：

```
量測了什麼
hash 是多少
extend 到哪個 PCR
量測順序
```

Debug 時：

```
PCR 不符合預期
  ↓
看 event log
  ↓
找出是哪個 image / config / cmdline 改變
```

----------

## 🔟 Attestation 是什麼？

Attestation 是：

```
把目前系統狀態證明給本機 policy 或遠端 server。
```

典型場景：

```
Device boot
  ↓
Measured Boot 產生 PCR / event log
  ↓
Device 送出 TPM quote / TEE attestation
  ↓
Server 比對 known-good state
  ↓
Allow / Deny / Limited mode
```

簡化圖：

```
[Device TPM / TEE]
        ↓ quote / attestation
[Remote Server]
        ↓ compare known-good values
[Decision]
```

用途：

```
確認 device 是否跑合法 firmware決定是否釋放 disk key決定是否允許連接 backend service決定是否進入 limited mode
```

----------

## 1️⃣1️⃣ Secure Boot + Measured Boot 可以一起用嗎？

可以，而且實務上常常一起使用。

```
Secure Boot:
  擋下不可信 image

Measured Boot:
  記錄實際載入內容

Attestation:
  對外證明目前狀態
```

組合 flow：

```
[BootROM]
    ↓ verify
[SPL / TF-A]
    ↓ verify + measure
[U-Boot]
    ↓ verify + measure
[Kernel / DTB / initramfs]
    ↓ measure
[TPM PCR / Event Log]
    ↓
[Attestation]
```

一句話：

```
Verify 是 gatekeeper。
Measure 是 audit trail。
Attestation 是 proof。
```

----------

## 1️⃣2️⃣ Embedded Linux 常見對應

Embedded Linux 常見組合：

```
BootROM secure boot
U-Boot FIT signature
signed Kernel / DTB / initramfs
dm-verity rootfs
TPM measured boot
signed update package
rollback protection
```

常見 flow：

```
[BootROM]
    ↓
[Signed SPL / TF-A]
    ↓
[Signed U-Boot]
    ↓
[Signed FIT image]
    ↓
[dm-verity RootFS]
```

BSP 工程師要特別確認：

```
DTB 是否包含在 FIT signature
initramfs 是否包含在 FIT signature
rootfs 是否有 dm-verity
update package 是否有簽章
舊版 signed image 是否會被 rollback protection 擋下
```

----------

## 1️⃣3️⃣ Android 常見對應

Android 常見組合：

```
Bootloader secure boot
AVB
vbmeta
dm-verity
rollback index
device lock state
KeyMint / Gatekeeper
RPMB
```

簡化 flow：

```
[Bootloader]
    ↓ verify vbmeta
[vbmeta]
    ↓ verify descriptors
[boot / vendor_boot / dtbo]
    ↓
[system / vendor / product / odm]
    ↓ dm-verity
[Android userspace]
```

BSP 工程師要特別確認：

```
bootloader 是否 locked
AVB 是否 enabled
vbmeta chain 是否完整
rollback index 是否更新
dm-verity 是否 enforcing
fastboot 是否允許 unsigned image
```

----------

## 1️⃣4️⃣ 常見錯誤

### ❌ 把 Secure Boot 和 Measured Boot 當成同一件事

```
Secure Boot 是 verify。
Measured Boot 是 measure。
```

兩者目的不同。

----------

### ❌ 有 Secure Boot，但 rootfs 沒驗證

```
Bootloader / kernel 都可信
但 rootfs 可以被任意修改
```

結果：

```
系統仍可能被植入 service / library / app。
```

----------

### ❌ 有 measurement，但沒有人檢查

```
PCR 有值
event log 有資料
但沒有 attestation / policy / baseline
```

結果：

```
只是記錄，沒有形成安全決策。
```

----------

### ❌ PCR 變了就直接判定被攻擊

PCR 改變不一定代表被攻擊。

也可能是：

```
kernel cmdline 改變
DTB 改變
firmware version 改變
build timestamp 改變
boot order 改變
event log 順序改變
```

所以要搭配 event log 分析。

----------

### ❌ 舊版 image 仍然可以開機

即使舊版 image 有合法簽章，也可能包含已知漏洞。

所以需要：

```
rollback protection
security version
rollback index
anti-rollback counter
```

----------

## 1️⃣5️⃣ BSP Debug：Secure Boot 檢查方向

### Step 1：確認驗證鏈

```
BootROM 是否驗證 SPL / TF-A？
SPL / TF-A 是否驗證 U-Boot？
U-Boot 是否驗證 Kernel / DTB / initramfs？
Kernel 是否驗證 rootfs？
Update path 是否驗證 package？
```

----------

### Step 2：確認驗證範圍

```
kernel 是否被簽章
DTB 是否被簽章
initramfs 是否被簽章
rootfs 是否有 dm-verity
Android vbmeta chain 是否包含所有必要 partition
```

----------

### Step 3：確認失敗時行為

```
signature mismatch 時會停在哪裡？
會不會 fallback 到 insecure boot？
會不會進入 unsigned recovery？
會不會允許 fastboot flash unsigned image？
```

----------

## 1️⃣6️⃣ BSP Debug：Measured Boot 檢查方向

### Step 1：確認 TPM / measurement backend

```
ls /sys/class/tpm/
dmesg | grep -i tpm
```

----------

### Step 2：讀 PCR

```
tpm2_pcrread
```

用途：

```
確認 PCR 是否有被 extend
不同 boot image 是否造成 PCR 改變
```

----------

### Step 3：看 Event Log

常見路徑：

```
/sys/kernel/security/tpm0/binary_bios_measurements
```

用途：

```
確認量測了哪些內容
找出 PCR 變化原因
確認 kernel / cmdline / initramfs 是否被量測
```

----------

### Step 4：確認 policy 是否存在

問自己：

```
誰會檢查 PCR？
誰保存 known-good baseline？
誰決定 allow / deny？
是否有 TPM quote / remote attestation？
是否有 key sealing？
```

----------


## 1️⃣7️⃣ 一張表總結

| 問題 | Secure Boot | Measured Boot | Attestation |
|------|-------------|----------------|-------------|
| 主要問題 | 能不能執行？ | 實際執行了什麼？ | 怎麼證明狀態？ |
| 主要動作 | Verify | Measure | Quote / prove |
| 失敗行為 | 通常停止開機 | 通常繼續開機 | 拒絕服務 / 不釋放 key |
| 主要資料 | signature / hash | PCR / event log | quote / certificate |
| 常見元件 | BootROM、U-Boot、AVB | TPM、firmware、bootloader | TPM、TEE、remote server |
| BSP 重點 | 驗證鏈完整性 | measurement 完整性 | policy / baseline |


