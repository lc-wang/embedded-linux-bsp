# hash_chain_minimal  
  
Root of Trust / Secure Boot 中最基本的 hash chain 範例。  
  
本章目的：  
  
> 理解為什麼「只算 hash」不夠，以及 hash chain 如何建立 boot flow 的完整性紀錄。  
  
---  
  
## 🎯 本章的目的  
  
理解：  
  
- hash 是什麼  
- hash chain 是什麼  
- 為什麼 boot flow 可以被一層一層量測  
- hash chain 與 Secure Boot / Measured Boot / dm-verity 的關係  
- 為什麼 hash 必須被保護才有安全意義  
  
---  
  
## 🧠 一句話先記住  
  
```text  
hash  
= 檢查資料有沒有變  
  
hash chain  
= 檢查一整串流程有沒有變
```

----------

## 🔄 基本模型

```
BootROM
 ↓ measure
TF-A
 ↓ measure
U-Boot
 ↓ measure
Kernel
 ↓ measure
RootFS
```

每一層都可以被 hash：

```
H(image)
```

然後串成：

```
chain = H(previous_chain || current_image_hash)
```

----------

## 🧠 為什麼需要 hash chain？

如果只記錄單一 hash：

```
H(kernel)
```

只能知道 kernel 有沒有變。

但 boot flow 是一整串：

```
TF-A
U-Boot
Kernel
DTB
RootFS
```

所以需要把每一步串起來。

----------

## 🔥 最重要觀念

hash chain 本身只能回答：

```
內容有沒有變？
```

但不能回答：

```
這個內容是不是可信？
```

要變成可信，需要：

-   digest 存在不可竄改位置
-   digest 被簽章
-   public key 存在 Root of Trust
-   TPM PCR 被 quote / attest

----------

## 🧩 對應真實技術

| 技術 | 對應概念 |  
|------|----------|  
| Secure Boot | 驗證簽章，決定能不能執行 |  
| Measured Boot | 記錄 hash，之後可證明開了什麼 |  
| TPM PCR | hash chain register |  
| dm-verity | rootfs block hash tree |  
| AVB | image hash + signature + rollback index |

----------

## ✅ Build

需要 OpenSSL development package：

```
sudo apt install libssl-dev
```

Build：

```
make
```

----------

## ✅ Run

建立測試檔案：

```
echo "TF-A image" > tfa.bin
echo "U-Boot image" > uboot.bin
echo "Linux kernel image" > Image
echo "Device Tree Blob" > board.dtb
```

執行：

```
./hash_chain_minimal tfa.bin uboot.bin Image board.dtb
```

你會看到每個 component 的 hash，以及最後的 chain digest。

----------

## ⚠ 注意

這個範例只是 hash chain model。

它還不是 Secure Boot。

因為 Secure Boot 還需要：

```
signature verification
+
trusted public key
```
