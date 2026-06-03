
# 🧠 Firmware Update

## 🎯 本章目的

本章要把 firmware update 中常見的安全元件串起來：

```
update package
signature verification
image hash
version check
rollback protection
A/B update
recovery
bootloader verification
rootfs verification
factory / field update
```

重點不是單純介紹 OTA 或 upgrade command，而是理解：

```
firmware update 怎麼納入 Chain of Trust？
為什麼 update package 也需要簽章？
為什麼只靠 Secure Boot 還不夠？
為什麼 recovery / factory update path 也要保護？
```

👉 Firmware update 是很多產品安全破口的來源。

----------

## 🧭 一張圖先看懂

```
[Update Package]
        ↓
[Verify package signature]
        ↓
[Check version / rollback index]
        ↓
[Check image hash]
        ↓
[Write inactive slot / target partition]
        ↓
[Mark boot attempt]
        ↓
[Reboot]
        ↓
[Bootloader verifies image]
        ↓
[Kernel verifies rootfs / partition]
        ↓
[Mark update successful]
```

一句話：

```
Firmware update security = 簽章驗證 + 版本檢查 + 寫入保護 + 開機驗證 + 回復機制。
```

----------

## 1️⃣ 為什麼 Firmware Update 需要安全設計？

Secure Boot 可以保護 boot path，但 firmware update 是另一條寫入系統的路徑。

如果 update path 不安全，攻擊者可以繞過正常開機驗證。

常見風險：

```
刷入 unsigned image
刷入被修改的 update package
刷回舊版有漏洞 image
recovery mode 接受不可信 image
factory tool 寫入錯誤 image
update 中斷造成系統無法開機
A/B slot 狀態管理錯誤
debug update command 未關閉
```

所以不能只問：

```
有沒有 Secure Boot？
```

還要問：

```
誰可以更新 firmware？
update package 有沒有驗證？
版本有沒有檢查？
recovery / factory path 有沒有同樣保護？
```

----------

## 2️⃣ Firmware Update 在 Chain of Trust 的位置

Boot chain 是：

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

Update chain 是：

```
Update Client / Recovery / Factory Tool
  ↓
Verify update package
  ↓
Write image
  ↓
Bootloader verify new image
  ↓
Kernel verify rootfs
```

兩者要接起來：

```
Update path 寫入的東西，
下一次 boot path 必須仍然能驗證。
```

否則會出現：

```
Update 時通過
Boot 時失敗
```

或更糟：

```
Update path 寫入了不該被允許的 image
```

----------

## 3️⃣ Firmware Update 基本安全流程

建議把 update flow 拆成幾個 step：

```
Step 1: Authenticate update source
Step 2: Verify update package signature
Step 3: Check version / rollback index
Step 4: Verify image hash / manifest
Step 5: Write image safely
Step 6: Reboot into updated slot
Step 7: Verify boot success
Step 8: Commit or rollback
```

簡化圖：

```
[Download / Receive Package]
        ↓
[Signature OK?]
        ↓
[Version OK?]
        ↓
[Image Hash OK?]
        ↓
[Write]
        ↓
[Boot Verify]
        ↓
[Commit]
```

----------

## 4️⃣ Update Package 應該保護什麼？

Update package 不應該只是壓縮檔。

它通常至少需要包含：

```
metadata / manifest
image hash
image version
target board / product id
partition layout
rollback index
signature
```

概念：

```
update-package
├── manifest.json
├── boot.img
├── dtbo.img
├── rootfs.img
├── firmware.bin
└── signature
```

Manifest 裡可以描述：

```
product = board-a
version = 1.2.3
security_version = 5
partitions = boot, rootfs
hash[boot.img] = ...
hash[rootfs.img] = ...
```

重點：

```
signature 應該保護 manifest。
manifest 應該保護每個 image hash。
```

----------

## 5️⃣ Signature Verification

Update package 必須做簽章驗證。

基本概念：

```
private key:
  build / release server 用來簽 update package

public key:
  device 端用來驗證 update package
```

Update flow：

```
[Package]
    ↓
[Read manifest]
    ↓
[Verify signature with public key]
    ↓
[Verify image hash]
    ↓
[Allow installation]
```

如果簽章失敗：

```
拒絕安裝
不要寫入任何 partition
保留目前可開機版本
```

----------

## 6️⃣ Image Hash Verification

只驗證 package signature 還不夠。

還需要確認每個 image 是否符合 manifest 中記錄的 hash。

流程：

```
[manifest]
    ↓ contains expected hash
[boot.img]
    ↓ calculate actual hash
[compare]
    ↓
match → write
mismatch → reject
```

原因：

```
避免 package 內 image 被替換
避免下載 / 傳輸過程中損壞
避免 manifest 和 image 不一致
```

----------

## 7️⃣ Version Check / Rollback Check

Firmware update 必須檢查版本。

不是為了美觀，而是為了防止：

```
rollback attack
```

攻擊者可能刷回舊版有漏洞 firmware：

```
v1.0 has CVE
v1.1 fixes CVE
attacker installs signed v1.0
```

所以 update 時要檢查：

```
new security_version >= stored security_version
```

常見保存位置：

```
eFuse / OTP
RPMB
TEE secure storage
TPM NV index
bootloader metadata
```

如果版本太舊：

```
拒絕安裝
不要更新 rollback counter
不要寫入舊 image
```

----------

## 8️⃣ Rollback Protection 與 Firmware Update 的關係

Rollback protection 通常有兩個時間點：

```
Update time:
  檢查 package version 是否允許安裝

Boot time:
  檢查 image rollback index 是否允許開機
```

也就是：

```
update client 要檢查
bootloader 也要檢查
```

原因：

```
攻擊者可能繞過 update client，
直接寫入 storage。
```

所以 bootloader 仍然需要做最後防線。

----------

## 9️⃣ A/B Update 是什麼？

A/B update 是常見安全更新設計。

系統有兩組 slot：

```
slot A
slot B
```

目前從 A 開機時，更新寫入 B。

```
Current boot: slot A
Update target: slot B
```

流程：

```
[Boot slot A]
    ↓
[Write update to slot B]
    ↓
[Mark slot B bootable]
    ↓
[Reboot into slot B]
    ↓
[If boot success]
    ↓
[Mark slot B successful]
```

如果 slot B 開機失敗：

```
bootloader fallback to slot A
```

----------

## 🔟 A/B Update Flow

```
[Running Slot A]
        ↓
[Verify update package]
        ↓
[Write Slot B]
        ↓
[Set Slot B active]
        ↓
[Reboot]
        ↓
[Boot Slot B]
        ↓
[Health check]
        ↓
+----------------------+
| success?             |
+----------+-----------+
           |
      yes  |  no
           | 
           v
[Mark B successful]     [Fallback to A]
```

重點：

```
不要直接覆蓋目前正在跑的 slot。
```

這樣可以降低 update 中斷或 image 錯誤造成 brick 的風險。

----------

## 1️⃣1️⃣ Recovery Path 也要保護

很多產品有 recovery mode，例如：

```
USB recovery
SD card recovery
Android recovery
U-Boot update command
factory flashing tool
UART download mode
```

這些路徑也必須有安全檢查。

否則會出現：

```
Normal OTA 有簽章驗證
但 recovery 可以刷 unsigned image
```

這等於整個 update security 被繞過。

Recovery path 應該確認：

```
update package signature
target product id
security version
rollback index
image hash
debug unlock state
```

----------

## 1️⃣2️⃣ Factory Update vs Field Update

Firmware update 可以分成：

| 類型 | 場景 | 風險 |
|------|------|------|
| Factory Update | 工廠燒錄 / 產線更新 | 燒錯 key、燒錯版本、未鎖 debug |
| Field Update | 出貨後 OTA / 客戶端更新 | 網路攻擊、rollback、package tamper |
| Recovery Update | 救援 / 維修 | 繞過正常安全檢查 |
| RMA Update | 維修中心更新 | debug unlock、資料外洩 |

不同場景可以有不同流程，但安全原則相同：
```
任何能寫入 firmware 的路徑都必須被控管。
```

----------

## 1️⃣3️⃣ Update Key Management

Firmware update 需要 signing key。

常見 key：

```
development key
test key
production key
recovery key
factory key
```

BSP / release 流程要避免：

```
test key 出現在 production device
private key 放在 build image
private key 放在 repo
所有產品共用同一把 key
無法 revoke 舊 key
```

建議原則：

```
private key 只存在 signing server / HSM
device 端只保存 public key 或 public key hash
test key 和 production key 分開
release image 必須可追蹤使用哪把 key 簽章
```

----------

## 1️⃣4️⃣ Key Rotation / Key Revocation

如果 signing key 外洩，就需要撤銷舊 key。

所以設計 update security 時，要考慮：

```
能不能換 key？
能不能撤銷舊 key？
device 如何信任新 key？
舊 firmware 是否仍接受舊 key？
```

常見設計：

```
key version
certificate chain
key manifest
multiple trusted keys
fuse-based key revocation
bootloader trusted key list
```

重點：

```
不要假設 production key 永遠不會出問題。
```

----------

## 1️⃣5️⃣ Update Metadata

Update metadata 很重要。

它通常用來記錄：

```
active slot
bootable slot
successful slot
retry count
rollback index
installed version
pending update state
```

A/B update 常見 metadata：

```
slot A:
  priority
  bootable
  successful
  retry_count

slot B:
  priority
  bootable
  successful
  retry_count
```

如果 metadata 損壞或設計不良，可能造成：

```
無限重開機
無法 fallback
rollback counter 錯誤
bootloader 選錯 slot
已失敗 image 被標記成功
```

----------

## 1️⃣6️⃣ Power Loss / Interrupted Update

Firmware update 必須考慮斷電。

危險情境：

```
正在寫 bootloader 時斷電
正在更新 rootfs 時斷電
metadata 寫一半斷電
rollback counter 更新後 image 沒寫完
```

設計重點：

```
atomic metadata update
write inactive slot
verify after write
boot success confirmation
avoid updating critical boot stage without recovery
```

對 BSP 工程師來說，要特別小心：

```
SPL / TF-A / U-Boot 更新
partition table 更新
eFuse / rollback counter 更新
```

這些失敗可能導致 device brick。

----------

## 1️⃣7️⃣ RootFS Verification

Firmware update 寫入 rootfs 後，boot 時仍然需要驗證。

Embedded Linux 常見：

```
dm-verity
read-only squashfs
signed rootfs
fs-verity
IMA / EVM
```

Android 常見：

```
AVB
vbmeta
dm-verity
hashtree
```

Update package 驗證只代表：

```
安裝時 image 是可信的。
```

RootFS verification 代表：

```
開機與 runtime 時 rootfs 沒被竄改。
```

兩者不同，最好都要有。

----------

## 1️⃣8️⃣ Debug / Development Mode

開發階段常見需求：

```
允許 unsigned image
允許 fastboot flash
允許 UART recovery
允許 test key
允許 debug shell
```

但 production 要確認：

```
development mode 是否關閉
bootloader 是否 locked
fastboot 是否限制
debug command 是否移除
test key 是否不可用
recovery 是否檢查簽章
```

常見錯誤：

```
production image 還接受 test key
bootloader unlock 不清資料
fastboot 可以刷 unsigned image
recovery 可以 sideload unsigned package
```

----------

## 1️⃣9️⃣ BSP Debug：Update Security 檢查方向

### Step 1：確認 update package 是否驗證

```
package 是否有 signature？
signature 保護哪些內容？
manifest 是否有被簽？
image hash 是否有檢查？
```

----------

### Step 2：確認版本與 rollback

```
package 是否有 security version？
device 是否保存 rollback index？
update time 是否檢查版本？
boot time 是否也檢查版本？
rollback counter 存在哪裡？
```

----------

### Step 3：確認寫入流程

```
是否寫 inactive slot？
寫完是否 verify？
metadata 是否 atomic？
斷電是否可恢復？
boot 成功後才 mark successful？
```

----------

### Step 4：確認所有 update path

```
OTA
USB update
SD card update
Android recovery
fastboot
factory flashing tool
U-Boot command
UART download mode
```

每一條都要問：

```
是否驗證簽章？
是否檢查 rollback？
是否限制 debug state？
```

----------

## 2️⃣0️⃣ 常見錯誤

### ❌ 只有 OTA 驗證，Recovery 不驗證

```
OTA package must be signed
Recovery accepts unsigned image
```

結果：

```
攻擊者走 recovery path 繞過 OTA security。
```

----------

### ❌ 只檢查 version string

例如：

```
version = "1.2.3"
```

但沒有不可回退的 security counter。

結果：

```
版本字串可以被修改或繞過。
```

安全版本應該和：

```
rollback index
security version
anti-rollback counter
```

搭配。

----------

### ❌ 更新 rollback counter 太早

如果先更新 rollback counter，再寫 image，斷電可能造成：

```
counter 已經提高
但新 image 沒寫完整
舊 image 因 rollback index 太低不能開
```

結果：

```
device brick
```

----------

### ❌ A/B slot 成功判斷太早

如果剛 boot 起來就 mark successful，但 service 還沒正常啟動，可能導致：

```
broken image 被標記成功
fallback 失效
```

應該等 health check 通過。

----------

### ❌ Factory tool 可以刷任何 image

工廠工具常有高權限。

如果沒有控管：

```
刷錯產品 image
刷入 test key image
刷入舊版 image
刷入 unsigned image
```

production 前要特別檢查 factory flow。

----------

## 2️⃣1️⃣ Firmware Update Security Checklist

```
[ ] Update package has signature
[ ] Manifest is included in signature scope
[ ] Image hash is verified before write
[ ] Product / board id is checked
[ ] Security version is checked
[ ] Rollback index is checked at update time
[ ] Rollback index is checked at boot time
[ ] Rollback counter storage is protected
[ ] A/B update writes inactive slot
[ ] Slot metadata update is atomic
[ ] Boot success is confirmed before marking successful
[ ] Recovery path verifies signature
[ ] Factory flashing path is controlled
[ ] Fastboot / U-Boot update commands are restricted in production
[ ] Test key and production key are separated
[ ] Key rotation / revocation is considered
[ ] Power loss during update is handled
[ ] RootFS / partition verification is enabled
```
