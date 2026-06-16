# Trace notes — hash_chain_minimal  
  
## 🟢 Level 1：用人話理解  
  
hash 可以想成：  
  
```text  
資料的指紋
```

如果資料變了：

```
hash 也會變
```

例如：

```
kernel image
 ↓
SHA256
 ↓
kernel hash
```

----------

## 🧠 hash chain 是什麼？

hash chain 是把每一步的 hash 串起來：

```
上一個 chain
+
目前 image hash
↓
新的 chain
```

所以任何一個 component 被改掉：

```
final chain digest 也會改變
```

----------

## 🟡 Level 2：流程理解

本範例輸入多個 component：

```
./hash_chain_minimal tfa.bin uboot.bin Image board.dtb
```

程式會做：

```
chain_0 = 0

measurement_1 = SHA256(tfa.bin)
chain_1 = SHA256(chain_0 || measurement_1)

measurement_2 = SHA256(uboot.bin)
chain_2 = SHA256(chain_1 || measurement_2)

measurement_3 = SHA256(Image)
chain_3 = SHA256(chain_2 || measurement_3)

measurement_4 = SHA256(board.dtb)
chain_4 = SHA256(chain_3 || measurement_4)
```

最後得到：

```
final chain digest
```

----------

## 🔥 為什麼這對 Root of Trust 重要？

Boot flow 不是單一檔案。

它通常是：

```
BootROM
 ↓
TF-A
 ↓
OP-TEE
 ↓
U-Boot
 ↓
Kernel
 ↓
DTB
 ↓
RootFS
```

如果只驗證其中一個 image，不足以描述整個開機狀態。

hash chain 可以把整個 boot sequence 壓成一個 digest。

----------

## 🔴 Level 3：對應真實系統

### Measured Boot

Measured Boot 的概念是：

```
不一定阻止開機
但會記錄開了什麼
```

每一層 boot component 被量測後，結果會被 extend 到紀錄中。

----------

### TPM PCR

TPM PCR 的概念類似：

```
PCR_new = SHA256(PCR_old || measurement)
```

這和本範例的：

```
chain_new = SHA256(chain_old || image_hash)
```

是同一種心智模型。

----------

### Secure Boot

Secure Boot 則更進一步：

```
hash
+
signature
+
trusted public key
```

它不只是記錄，而是決定：

```
能不能執行
```

----------

## 🚫 常見誤解

### ❌ hash 一樣就代表可信

不完全對。

hash 一樣只能表示：

```
內容沒變
```

但不能表示：

```
內容可信
```

除非你知道正確 hash 來自可信來源。

----------

### ❌ hash chain 等於 Secure Boot

不對。

hash chain 是 measurement model。

Secure Boot 還需要：

```
signature verification
trusted key
enforcement policy
```

----------

## 🧠 最重要一句話

```
hash chain 可以描述「開機流程變了沒有」

但 Root of Trust 必須保護：
最初的 key、policy 或 digest
```
