
# 🧠 Rollback Protection

## 🎯 本章目的

本章要把以下幾個概念串起來：

```
rollback attack
rollback index
security version
anti-rollback counter
eFuse / OTP
RPMB
TEE secure storage
TPM NV index
AVB rollback index
update-time check
boot-time check
```

重點不是只知道「版本不能降版」，而是理解：

```
為什麼已簽章的舊 image 仍然可能危險？
rollback index 存在哪裡才安全？
為什麼 update time 和 boot time 都要檢查？
為什麼 counter 更新時機會影響是否 brick？
```

👉 Rollback Protection 是 Secure Boot / Firmware Update Security 的重要補強。

----------

## 🧭 一張圖先看懂

```
[Current device security version = 5]
        ↓
[Install package security version = 3]
        ↓
[Signature OK]
        ↓
[Rollback check failed]
        ↓
[Reject update / reject boot]
```

一句話：

```
Rollback Protection 是防止 signed old vulnerable image 被重新安裝或開機。
```

----------

## 1️⃣ 為什麼需要 Rollback Protection？

Secure Boot 可以確認：

```
image 是不是由可信 key 簽章？
```

但它不一定能判斷：

```
這個 image 是不是太舊？這個版本是不是有已知漏洞？
```

例如：

```
v1.0:
  signed image
  contains security bug

v1.1:
  signed image
  fixes security bug
```

如果 device 已經更新到 v1.1，但仍允許開機 v1.0：

```
攻擊者可以刷回 v1.0  
利用舊漏洞攻擊系統
```

這就是 rollback attack。

----------

## 2️⃣ Secure Boot vs Rollback Protection


| 項目 | Secure Boot | Rollback Protection |  
|------|-------------|---------------------|  
| 核心問題 | image 是否可信（key 簽章）？ | image 是否太舊？ |  
| 防禦對象 | unsigned / modified image | signed but vulnerable old image |  
| 檢查資料 | signature / public key hash | security version / rollback index |  
| 失敗行為 | 不執行 image | 不安裝或不開機 |  
| 常見位置 | BootROM / bootloader / AVB | bootloader / TEE / RPMB / eFuse |

簡化理解：

```
Secure Boot:
  這是不是我們簽的？

Rollback Protection:
  這是不是允許的最低安全版本以上？
```

----------

## 3️⃣ Rollback Attack Flow

攻擊者的目標不是偽造簽章，而是利用舊版合法 image。

```
[Device currently runs v2]
        ↓
[v2 has security fix]
        ↓
[Attacker obtains signed v1 image]
        ↓
[v1 has known vulnerability]
        ↓
[Attacker flashes v1]
        ↓
[Device boots v1]
        ↓
[Exploit old vulnerability]
```

所以只靠 signature verification 不夠。

需要額外檢查：

```
image security_version >= device stored rollback_index
```

----------

## 4️⃣ Rollback Index 是什麼？

Rollback index 可以理解成：

```
device 允許開機的最低安全版本。
```

例如：

```
device rollback_index = 5
```

代表：

```
security_version >= 5 的 image 才允許安裝或開機
```

如果 image 是：

```
image security_version = 3
```

結果：

```
reject
```

----------

## 5️⃣ Version String vs Security Version

一般版本號不一定適合做 rollback protection。

例如：

```
version = "1.2.3"
```

這只是人看的版本。

rollback protection 通常需要一個可比較、不可任意降低的 security version：

```
security_version = 5
rollback_index = 5
anti_rollback_counter = 5
```


| 項目 | Version String | Security Version |  
|------|----------------|------------------|  
| 用途 | 顯示版本 | 安全判斷 |  
| 格式 | 1.2.3 / build tag | integer / monotonic counter |  
| 是否適合比較 | 不一定 | 適合 |  
| 是否用於 rollback | 不建議 | 建議 |  
| 是否可能跟 release 版本相同 | 可能 | 不一定 |

重點：

```
不是每次功能更新都一定要提高 security version。
但修補安全漏洞後，通常需要提高 security version。
```

----------

## 6️⃣ Rollback Check 有兩個時間點

Rollback check 不應該只出現在 update client。

通常要有兩層：

```
Update-time check
Boot-time check
```

----------

### Update-time check

發生在安裝 firmware 時。

```
[Update package]
    ↓
[Check package security_version]
    ↓
[Compare stored rollback_index]
    ↓
[Allow / Reject install]
```

目的：

```
不要把明顯過舊的 image 寫進 storage。
```

----------

### Boot-time check

發生在 bootloader 開機時。

```
[Bootloader]
    ↓
[Read image rollback index]
    ↓
[Read stored rollback index]
    ↓
[Allow / Reject boot]
```

目的：

```
防止攻擊者繞過 update client，
直接寫入 storage 或切換 slot。
```

一句話：

```
Update-time check 是第一道門。
Boot-time check 是最後防線。
```

----------

## 7️⃣ Rollback Counter 應該存在哪裡？

Rollback counter 必須存在 Normal World 不能任意修改的位置。

常見選項：

```
eFuse / OTP
RPMB
TEE secure storage
TPM NV index
secure element
bootloader protected metadata
```

----------

### eFuse / OTP

優點：

```
硬體保護強
不可被普通軟體降低
適合保存不可逆安全狀態
```

限制：

```
寫入次數有限
通常不可逆
不適合頻繁更新
燒錯可能無法恢復
```

適合：

```
重要 security version
key hash
secure boot enable
production lock state
```

----------

### RPMB

優點：

```
支援 replay protection
比 eFuse 更適合保存可更新 counter
常與 OP-TEE secure storage 搭配
```

限制：

```
需要 RPMB key provisioning
storage device 要支援
bring-up 較複雜
```

適合：

```
rollback counter
secure storage metadata
Android rollback index
TEE-backed counter
```

----------

### TPM NV Index

優點：

```
適合 TPM-based platform
可搭配 policy
可保存安全狀態
```

限制：

```
embedded ARM 平台不一定有 TPM
需要 TPM provisioning / policy 設計
```

適合：

```
measured boot policy
device state
sealed key version
```

----------

## 8️⃣ Counter 更新時機很重要

Rollback counter 不是越早更新越好。

錯誤流程：

```
[Verify package]
    ↓
[Update rollback counter to 6]
    ↓
[Write image]
    ↓
[Power loss]
```

可能結果：

```
counter 已經變成 6
但新 image 沒寫完整
舊 image security_version = 5
bootloader 拒絕舊 image
device brick
```

----------

## 9️⃣ 較安全的更新思路

A/B update 通常會搭配：

```
write inactive slot
verify written image
boot new slot
health check
mark successful
then commit security state
```

概念 flow：

```
[Running slot A, rollback_index = 5]
        ↓
[Install slot B, security_version = 6]
        ↓
[Verify slot B image]
        ↓
[Reboot into slot B]
        ↓
[Health check OK]
        ↓
[Commit rollback_index = 6]
```

重點：

```
不要在新 image 還沒確認可用前，
讓舊 image 永遠不能回去。
```

實際平台策略會因 SoC / bootloader / AVB / update framework 而不同。

----------

## 🔟 A/B Slot 與 Rollback Protection

A/B update 中，slot metadata 和 rollback index 要一起設計。

常見狀態：

```
slot A:
  successful = yes
  security_version = 5

slot B:
  successful = no
  security_version = 6
```

更新後：

```
slot B bootable
slot B retry_count = 3
slot B successful = no
```

開機成功後：

```
slot B successful = yes
rollback_index = 6
```

如果 slot B 開機失敗：

```
fallback to slot A
rollback_index 不應該太早提高到 6
```

否則 slot A 可能因版本過低而不能開。

----------

## 1️⃣1️⃣ Android AVB Rollback Index

Android Verified Boot 有 rollback index 概念。

簡化 flow：

```
[Bootloader]
    ↓
[Verify vbmeta signature]
    ↓
[Read rollback_index from vbmeta]
    ↓
[Read stored rollback index]
    ↓
[Compare]
    ↓
[Allow / Reject boot]
```

概念：

```
vbmeta rollback_index >= stored rollback_index
```

如果通過，bootloader 才允許繼續開機。

Android 常見保存位置：

```
RPMB
tamper-resistant storage
SoC secure storage
```

BSP 要確認：

```
rollback index location 是否正確
bootloader 是否真的檢查
OTA 是否更新 rollback index
fastboot / unlocked state 是否影響 policy
```

----------

## 1️⃣2️⃣ Embedded Linux 常見做法

Embedded Linux 沒有固定唯一標準。

常見組合：

```
U-Boot verified boot
FIT image signature
custom security_version in FIT config
dm-verity rootfs
RAUC / Mender / SWUpdate
RPMB / eFuse / TPM NV counter
```

簡化 flow：

```
[Update package]
    ↓ verify signature
[Read package security_version]
    ↓ compare stored counter
[Write inactive slot]
    ↓
[U-Boot verify FIT signature]
    ↓
[U-Boot check security_version]
    ↓
[Boot kernel + dm-verity rootfs]
```

重點：

```
Linux BSP 需要自己定義清楚 security_version 放哪裡、誰檢查、counter 存哪裡。
```

----------

## 1️⃣3️⃣ Recovery / Factory Path 也要檢查

Rollback protection 不能只做在 OTA。

所有能寫入 image 的路徑都要檢查：

```
OTA
recovery update
USB update
SD card update
factory flashing tool
fastboot
U-Boot command
UART download mode
```

常見錯誤：

```
OTA 拒絕舊版本
但 recovery 允許刷舊版本
```

結果：

```
rollback protection 被繞過。
```

----------

## 1️⃣4️⃣ Debug / Development Mode 注意事項

開發階段常需要：

```
允許降版測試
允許刷 test image
允許 fastboot flash
允許 recovery tool
```

但 production 要確認：

```
是否仍允許 rollback？
是否仍接受 test key？
是否仍允許 unsigned image？
是否仍允許 debug unlock 不清資料？
```

如果 production device 允許任意降版：

```
rollback protection 實際上不存在。
```

----------

## 1️⃣5️⃣ BSP Debug：Rollback Protection 檢查方向

### Step 1：確認 image 裡的 security version

檢查：

```
bootloader image version
kernel / FIT security_version
vbmeta rollback_index
rootfs image version
update package security_version
```

要知道：

```
哪個欄位是真正參與安全判斷？
哪個只是顯示用？
```

----------

### Step 2：確認 device stored rollback index

問自己：

```
stored rollback index 存在哪裡？
eFuse？
RPMB？
TEE secure storage？
TPM NV？
bootloader metadata？
```

並確認：

```
Normal World 能不能任意改低？
是否防 replay？
是否可被 factory reset 清掉？
```

----------

### Step 3：確認 update-time check

```
安裝舊版 package 是否會被拒絕？
簽章正確但 security_version 較低是否會被拒絕？
錯誤時是否不寫入 partition？
```

----------

### Step 4：確認 boot-time check

```
直接把舊版 image 寫進 storage 是否會被 bootloader 擋下？
切換到舊 slot 是否會被拒絕？
vbmeta rollback_index 過低是否會 boot fail？
```

----------

### Step 5：確認 counter 更新時機

```
counter 是安裝時更新？
第一次成功開機後更新？
health check 通過後更新？
```

尤其要檢查：

```
更新 counter 後斷電是否會 brick？
fallback slot 是否仍可用？
```

----------

## 1️⃣6️⃣ 常見錯誤

### ❌ 只靠 version string

```
version = "1.2.3"
```

但沒有不可降低的 security counter。

結果：

```
攻擊者可能透過舊版 signed image rollback。
```

----------

### ❌ Counter 存在普通 rootfs

如果 rollback counter 放在：

```
/etc/version
/var/lib/update/version
普通 ext4 partition
```

攻擊者可能直接改低或 replay 舊資料。

----------

### ❌ 只在 OTA 檢查，不在 bootloader 檢查

攻擊者可能繞過 OTA：

```
直接寫 storage
用 recovery
用 factory tool
切換 slot
```

所以 bootloader 也要檢查。

----------

### ❌ 太早提高 rollback counter

可能造成：

```
新 image 還沒確認成功
舊 image 已經不能開
device brick
```

----------

### ❌ 測試 key / debug mode 沒關

production 還允許：

```
test key image
unsigned image
fastboot flash
unlocked boot without wipe
```

rollback protection 可能被繞過。

----------

## 1️⃣7️⃣ Rollback Protection Checklist

```
[ ] Image security_version / rollback_index is defined
[ ] Security version is separate from display version if needed
[ ] Stored rollback index location is protected
[ ] Stored rollback index cannot be decreased by Normal World
[ ] Stored rollback index has replay protection if writable
[ ] Update package includes security_version
[ ] Update-time rollback check exists
[ ] Boot-time rollback check exists
[ ] Old signed image is rejected
[ ] Recovery path checks rollback index
[ ] Factory tool checks rollback index or is access-controlled
[ ] A/B slot fallback is compatible with rollback counter update timing
[ ] Rollback counter is not updated too early
[ ] Power loss during update does not brick device
[ ] Debug / development mode is disabled in production
[ ] Android AVB rollback index is configured if using AVB
[ ] Embedded Linux update framework has a defined rollback policy
```
