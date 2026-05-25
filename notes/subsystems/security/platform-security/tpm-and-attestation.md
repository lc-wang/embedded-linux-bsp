
# 🧠 TPM and Attestation

## 🎯 本章目的

本章要把以下幾個概念串起來：

```
TPM
PCR
Event Log
Measured Boot
Quote
Attestation
Key Sealing
Known-good baseline
```

重點不是介紹所有 TPM command，而是理解：

```
TPM 如何記錄開機狀態？
PCR 為什麼不能直接被軟體任意改寫？
Event Log 為什麼需要存在？
Remote Attestation 怎麼判斷 device 是否可信？
```

👉 這一章是理解 Measured Boot、Remote Attestation、Key Sealing 的基礎。

----------

## 🧭 一張圖先看懂

```
[Bootloader / Firmware]
        ↓ measure hash
[TPM PCR extend]
        ↓
[Linux boots]
        ↓
[Read PCR + Event Log]
        ↓
[TPM Quote]
        ↓
[Remote Server / Local Policy]
        ↓
[Compare known-good baseline]
        ↓
[Allow / Deny / Release key]
```

一句話：

```
TPM 負責保存 measurement。
Event Log 負責解釋 measurement。
Attestation 負責證明目前 device state。
```

----------

## 1️⃣ TPM 是什麼？

TPM 是：

```
Trusted Platform Module
```

可以理解成一個受保護的安全元件，用來提供：

```
secure key storage
PCR measurement
random number generation
key sealing
attestation
anti-tamper state reporting
```

在平台上可能是：

```
Discrete TPM:
  外接 TPM chip，例如 SPI / I2C TPM

Firmware TPM:
  由 firmware / TEE / secure world 實作

Integrated TPM:
  SoC 內建安全模組
```

對 BSP 工程師來說，TPM 最常見的用途是：

```
Measured Boot
Remote Attestation
Disk key sealing
Device identity
```

----------

## 2️⃣ TPM 和 Secure Boot 的差異

TPM 主要不是拿來「擋 image 執行」。

它更常用來：

```
記錄系統實際載入了什麼
保存可信狀態
讓本機或遠端驗證目前系統狀態
```


| 項目 | Secure Boot | TPM / Measured Boot |  
|------|-------------|---------------------|  
| 核心動作 | verify | measure |  
| 目的 | 不可信 image 不執行 | 記錄實際執行內容 |  
| 失敗時 | 通常停止開機 | 通常繼續開機 |  
| 產物 | pass / fail | PCR / event log |  
| 後續用途 | 建立執行信任鏈 | attestation / key sealing |

----------

## 3️⃣ PCR 是什麼？

PCR 是：

```
Platform Configuration Register
```

它是 TPM 內部用來保存 measurement 結果的 register。

PCR 的特性：

```
不能被一般軟體任意寫成指定值
通常只能透過 extend 更新
reset 條件受 TPM / platform policy 控制
```

extend 概念：

```
PCR_new = Hash(PCR_old || measurement)
```

意思是：

```
載入內容會影響 PCR
載入順序也會影響 PCR
```

所以 PCR 可以表示：

```
這台 device 目前開機路徑的摘要
```

----------

## 4️⃣ PCR Extend Flow

Measured Boot 中常見流程：

```
[Bootloader]
    ↓ calculate hash
[Kernel image hash]
    ↓ extend
[TPM PCR]

[Bootloader]
    ↓ calculate hash
[DTB hash]
    ↓ extend
[TPM PCR]

[Bootloader]
    ↓ calculate hash
[Kernel cmdline hash]
    ↓ extend
[TPM PCR]

[Bootloader]
    ↓ calculate hash
[initramfs hash]
    ↓ extend
[TPM PCR]
```

簡化後：

```
measure component
  ↓
extend PCR
  ↓
record event log
  ↓
continue boot
```

重點：

```
PCR 保存的是累積結果。
Event Log 保存的是每一次 measurement 的明細。
```

----------

## 5️⃣ Event Log 是什麼？

PCR 只有最後的 hash 結果。

但 debug 時你會想知道：

```
到底是哪個元件被量測？
哪個 hash 改變？
量測順序是什麼？
extend 到哪個 PCR？
```

所以需要 Event Log。

Event Log 常見內容：

```
PCR index
event type
measured component name
hash algorithm
digest value
event data
```

概念：

```
PCR:
  最終摘要

Event Log:
  產生這個摘要的明細
```

----------

## 6️⃣ 為什麼只有 PCR 不夠？

假設 PCR 值變了：

```
PCR changed
```

你無法只靠 PCR 判斷原因。

可能原因：

```
kernel image changed
DTB changed
kernel cmdline changed
initramfs changed
bootloader version changed
firmware config changed
event order changed
```

所以實務 debug flow 是：

```
PCR mismatch
  ↓
read event log
  ↓
replay event log
  ↓
compare digest
  ↓
find changed component
```

----------

## 7️⃣ Measured Boot 基本流程

完整一點的 measured boot flow：

```
[Power On]
    ↓
[Firmware / Bootloader starts]
    ↓
[Measure next stage]
    ↓
[Extend hash into TPM PCR]
    ↓
[Append event into Event Log]
    ↓
[Load next stage]
    ↓
[Linux boots]
    ↓
[Userspace reads PCR + Event Log]
```

注意：

```
Measured Boot 不一定阻止開機。
它只是把實際載入內容記錄下來。
```

是否要拒絕服務，要看後面的 policy。

----------

## 8️⃣ Attestation 是什麼？

Attestation 是：

```
證明目前平台狀態的機制。
```

常見問題：

```
這台 device 現在跑的是不是我們允許的 firmware？
kernel 有沒有被替換？
boot args 有沒有被改？
rootfs verification 有沒有啟用？
```

Attestation 的目標不是單純讀 PCR，而是：

```
用 TPM 簽出一份可信證明。
```

----------

## 9️⃣ TPM Quote 是什麼？

TPM Quote 是 attestation 的核心動作之一。

它會讓 TPM 對指定 PCR 值做簽章。

概念 flow：

```
[Verifier]
    ↓ send nonce
[Device]
    ↓ ask TPM quote PCRs
[TPM]
    ↓ sign PCR values + nonce
[Device]
    ↓ send quote + event log
[Verifier]
    ↓ verify signature
    ↓ replay event log
    ↓ compare known-good baseline
```

重點：

```
Quote 可以證明 PCR 值來自 TPM。
Nonce 可以防止 replay old quote。
```

----------

## 🔟 Remote Attestation Flow

典型 remote attestation：

```
Remote Server
    ↓ challenge nonce
Device
    ↓ TPM quote
TPM
    ↓ sign PCR state
Device
    ↓ send quote + event log + certificate
Remote Server
    ↓ verify quote signature
    ↓ verify nonce
    ↓ replay event log
    ↓ compare baseline
    ↓ allow / deny
```

簡化圖：

```
[Device TPM]
    ↓ quote PCR
[Device Agent]
    ↓ send quote + log
[Remote Verifier]
    ↓ verify
[Policy Decision]
```

Policy decision 可能是：

```
allow normal service
deny connection
release disk key
release application secret
enter limited mode
trigger remediation
```

----------

## 1️⃣1️⃣ Known-good Baseline 是什麼？

Known-good baseline 是：

```
被允許的 firmware / bootloader / kernel / config measurement 集合。
```

也就是 verifier 會拿 device 回報的狀態去比對：

```
目前 PCR / event log
  vs
已知可信版本 baseline
```

baseline 通常會隨版本更新。

例如：

```
Product A firmware v1.0
Product A firmware v1.1
Product A firmware v1.2 security fix
```

每個版本可能都有不同 baseline。

----------

## 1️⃣2️⃣ Key Sealing 是什麼？

Key Sealing 是 TPM 另一個常見用途。

概念：

```
把 key 綁定到特定 PCR 狀態。
```

例如 disk encryption key 只有在以下狀態才釋放：

```
PCR0 = expected firmware state
PCR4 = expected bootloader state
PCR8 = expected kernel cmdline state
PCR9 = expected initramfs state
```

flow：

```
[Boot]
    ↓
[Measured Boot updates PCR]
    ↓
[Request unseal disk key]
    ↓
[TPM checks PCR policy]
    ↓
[If PCR match → release key]
    ↓
[If PCR mismatch → deny]
```

重點：

```
不是單純把 key 存進 TPM。
而是讓 key 只在特定 boot state 下能被取出。
```

----------

## 1️⃣3️⃣ TPM 在 Embedded Linux 的位置

Embedded Linux 常見架構：

```
BootROM
  ↓
Bootloader
  ↓ measure kernel / dtb / initramfs / cmdline
TPM PCR
  ↓
Linux
  ↓
tpm driver
  ↓
userspace attestation agent
```

Linux 中常見元件：

```
/dev/tpm0
/dev/tpmrm0
/sys/class/tpm/
securityfs event log
tpm_tis / tpm_crb / spi-tpm driver
tpm2-tools
```

常見用途：

```
read PCR
parse event log
perform quote
seal / unseal secret
```

----------

## 1️⃣4️⃣ Linux TPM Debug

### Step 1：確認 TPM device

```
ls -l /dev/tpm*
ls /sys/class/tpm/
```

常見結果：

```
/dev/tpm0
/dev/tpmrm0
/sys/class/tpm/tpm0
```


| Node | 說明 |  
|------|------|  
| `/dev/tpm0` | TPM character device |  
| `/dev/tpmrm0` | TPM resource manager device，通常 userspace 工具較常用 |

----------

### Step 2：看 kernel log

```
dmesg | grep -i tpm
```

常見 driver 關鍵字：

```
tpm_tis
tpm_crb
tpm_tis_spi
```

如果是 SPI TPM，要同時確認：

```
SPI controller probe
TPM device tree / ACPI node
IRQ / reset GPIO
clock / regulator
```

----------

### Step 3：讀 PCR

```
tpm2_pcrread
```

用途：

```
確認 PCR 是否存在
確認 PCR 是否有被 extend
比較不同 boot image 對 PCR 的影響
```

----------

### Step 4：讀 Event Log

常見路徑：

```
/sys/kernel/security/tpm0/binary_bios_measurements
```

常用工具：

```
tpm2_eventlog /sys/kernel/security/tpm0/binary_bios_measurements
```

用途：

```
確認量測了哪些 component
確認 event log 是否和 PCR 對得起來
找出 PCR mismatch 原因
```

----------

### Step 5：產生 Quote

概念指令：

```
tpm2_quote
```

用途：

```
讓 TPM 對指定 PCR 狀態簽章
提供給 verifier 做 attestation
```

實務上通常會由 attestation agent 管理 nonce、AIK、certificate、quote 格式。

----------

## 1️⃣5️⃣ Device Tree / Driver 注意事項

如果是 discrete TPM，BSP 常見要確認：

```
TPM bus:
  SPI / I2C / LPC / memory mapped

TPM driver:
  tpm_tis
  tpm_tis_spi
  tpm_crb

GPIO:
  reset GPIO
  interrupt GPIO

Power:
  regulator
  clock
  pinctrl
```

SPI TPM 可能會遇到：

```
TPM probe timeout
wrong SPI mode
IRQ not firing
reset timing wrong
TPM locality error
```

Debug 時不要只看 TPM driver，也要看：

```
SPI controller
pinctrl
clock
regulator
interrupt routing
```

----------

## 1️⃣6️⃣ Attestation 常見錯誤

### ❌ 有 PCR，但沒有 Event Log

結果：

```
可以看到 PCR 值
但很難知道 PCR 為什麼變了
```

改善：

```
確認 firmware / bootloader 有產生 event log
確認 Linux securityfs 有掛載
確認 event log 有被傳給 verifier
```

----------

### ❌ 有 Event Log，但沒有 Policy

結果：

```
系統有記錄，但沒有任何安全決策
```

需要補：

```
known-good baseline
local policy
remote verifier
key sealing policy
```

----------

### ❌ PCR 每次開機都不同

可能原因：

```
kernel cmdline 每次變動
boot counter 被量測
timestamp 被量測
random seed 被量測
event order 不穩定
firmware variable 改變
```

Debug 方式：

```
比較 event log
找出哪個 event digest 每次不同
確認該 event 是否應該被納入 policy
```

----------

### ❌ Quote 沒有 nonce

如果 quote 沒有 challenge nonce，可能被 replay。

正確做法：

```
verifier 產生 nonce
device quote 時包含 nonce
verifier 檢查 nonce 是否一致
```

----------

### ❌ Baseline 沒有版本管理

Firmware update 後 PCR 合理變化。

如果 baseline 沒更新：

```
新版本可能被誤判為不可信
```

所以 baseline 要和：

```
firmware version
bootloader version
kernel version
configuration version
security patch level
```

一起管理。

----------


## 1️⃣7️⃣ TPM vs OP-TEE 的差異  
  
TPM 和 OP-TEE 都跟平台安全有關，但角色不同。  
  
| 項目 | TPM | OP-TEE |  
|------|-----|--------|  
| 類型 | 安全模組 / TPM implementation | TEE OS |  
| 主要位置 | Discrete chip / firmware TPM / SoC security block | ARM Secure World |  
| 強項 | PCR、attestation、key sealing | secure service、TA、secure storage、crypto |  
| 常見介面 | `/dev/tpm0`, `/dev/tpmrm0` | `/dev/tee0`, `/dev/teepriv0` |  
| 常見用途 | measured boot、quote、sealed key | KeyMint、Gatekeeper、TA、RPMB |  
| 是否需要 TrustZone | 不一定 | 通常需要 ARM TrustZone |

一句話：

```
TPM 偏向記錄與證明平台狀態。
OP-TEE 偏向提供 Secure World runtime 與安全服務。
```

----------

## 1️⃣8️⃣ BSP Checklist

```
[ ] TPM hardware / fTPM implementation is identified
[ ] TPM driver is enabled
[ ] /dev/tpm0 or /dev/tpmrm0 exists
[ ] TPM appears in /sys/class/tpm/
[ ] PCR values can be read
[ ] Event log exists
[ ] Event log can be parsed
[ ] Bootloader measures kernel
[ ] Bootloader measures DTB
[ ] Bootloader measures initramfs if used
[ ] Bootloader measures kernel cmdline
[ ] PCR value changes when measured component changes
[ ] PCR value is stable when boot inputs are unchanged
[ ] Quote flow includes nonce
[ ] Known-good baseline is defined
[ ] Baseline is versioned with firmware release
[ ] Policy decision is defined
[ ] Key sealing policy is defined if used
```
