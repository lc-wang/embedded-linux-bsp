# Trusted Firmware-A Memory Layout 與 MMU 架構分析

在 TF-A bring-up 過程中，  
**MMU 與記憶體配置（memory layout）是造成 early console 失效的最常見原因之一**。

常見症狀包括：

- early console 有輸出，但某一行之後完全消失
- BL2 有 log，進入 BL31 後無任何輸出
- MMU enable 之後 UART 寫入沒有反應
- console 在 cache / MMU 開啟後變成亂碼或卡死

要正確理解這些問題，必須先建立一個清楚的模型：

> **TF-A 並非「一開始就有完整 MMU 與虛擬記憶體」。**

本章目的在於說明：

- TF-A 各階段的記憶體配置概念
- MMU 啟用前後的差異
- UART / console 與 MMU 的關係
- 為何 console 常在 MMU enable 後失效
- TF-A 如何確保 MMIO 區域在 MMU 後仍可存取

## 1. TF-A 的記憶體觀念總覽

TF-A 的記憶體使用可概念化為三個階段：
```
(1) MMU disabled
(2) MMU enabled, minimal mapping
(3) MMU enabled, full runtime mapping
```

每個階段對 address 的解讀方式完全不同。

## 2. MMU 啟用前：Physical Address 世界

在 MMU 尚未啟用前：

- CPU 使用 **physical address**
- 沒有 virtual address translation
- 沒有 page table
- 沒有 cache policy 控制

因此：
```
MMIO access = write physical address directly
```

這也是 early console 能夠極早期運作的原因。

### 2.1 Early console 的條件

- UART base 為實體位址
- 不需要 mapping
- 不需要 cache 設定
- 只要 clock / pinmux 正確即可輸出

## 3. 為什麼一定要啟用 MMU？

TF-A 仍然需要 MMU，原因包括：

- 記憶體存取保護
- cache enable（效能）
- 區分 device memory / normal memory
- EL3 runtime service 穩定性
- 防止 speculative access 問題

因此在 BL2 / BL31 過程中，  
**MMU 一定會被啟用**。

## 4. MMU 啟用後會發生什麼事？

當 MMU 啟用後：

- CPU 使用 **virtual address**
- 所有 memory access 都必須透過 page table
- 未 mapping 的 address → data abort

此時：

> **UART MMIO 區域如果沒有被正確 mapping，console 立即失效。**

這正是 console 常「突然消失」的根本原因。

## 5. TF-A Memory Layout 的組成

TF-A 使用 `mmap_region_t` 描述 memory layout：

```c
typedef struct mmap_region {
    uintptr_t base_pa;
    uintptr_t base_va;
    size_t size;
    unsigned int attr;
} mmap_region_t;
```

每一個 region 定義：

-   physical address

-   virtual address

-   size

-   memory attribute（device / normal / cache）

## 6. MMIO（UART）在 TF-A 中的 mapping

UART 屬於：

`Device memory` 

其典型屬性為：

-   non-cacheable

-   strongly ordered 或 device

-   no speculative access

因此必須被明確加入 MMU mapping。

### 6.1 範例：

```
MAP_REGION_FLAT(UART_BASE,
                UART_SIZE,
                MT_DEVICE | MT_RW | MT_SECURE)
```
若此 mapping 缺失，則：

-   MMU enable 前：console 正常

-   MMU enable 後：console 完全失效

## 7. Early Console 與 MMU 的關係

### 7.1 Early console 為何能在 MMU 前使用？

因為：

-   使用 physical address

-   不經過 page table

-   直接 MMIO write

### 7.2 為何不能依賴 early console？

因為：

-   early console **無法跨越 MMU enable**

-   MMU 啟用後，必須改用已 mapping 的 console

因此 TF-A 設計上：

> **early console 只用於 early bring-up，而非整個 boot flow。**

## 8. BL2 / BL31 中 MMU 初始化的位置

典型流程如下：
```
BL2
 ├─ early console (MMU off)
 ├─ memory discovery
 ├─ build page tables
 ├─ enable MMU
 └─ runtime console

BL31
 ├─ early console (MMU off)
 ├─ setup EL3 memory
 ├─ enable MMU
 └─ runtime console
```
關鍵點：

-   **MMU enable 是 stage-specific**

-   BL2 與 BL31 各自建立 page table

-   memory mapping 不會自動繼承

## 9. 為何 BL2 console 正常，BL31 console 卻壞？

常見原因包括：

-   BL31 沒有 mapping UART MMIO

-   BL31 page table 與 BL2 不一致

-   BL31 使用不同的 VA base

-   BL31 的 memory region size 不包含 UART

因此必須理解：

> **BL31 是全新的 MMU 世界。**

## 10. Device memory 與 Normal memory 的差異

UART MMIO 必須使用 device memory attribute。

若錯誤設為 normal memory：

-   cache 可能導致寫入延遲

-   speculative access 可能造成 exception

-   console 行為不穩定

這也是 TF-A 對 MMIO mapping 非常嚴格的原因。

## 11. 為何 console 問題常與 cache 一起出現？

因為：

-   cache 只能用於 normal memory

-   device memory 必須 bypass cache

-   錯誤 mapping 會導致：

```
write buffer 未 flush
UART register 寫入失效
```
造成「偶爾有字、偶爾沒字」的假象。

## 12. 常見問題與排查（MMU 與 console 問題的典型除錯方向）

當 console 在某一階段消失時，優先檢查：

1.  MMU 是否已啟用？

2.  UART 是否在 page table 中？

3.  UART region 是否為 device memory？

4.  BL2 / BL31 是否使用不同 mapping？

5.  是否在 MMU enable 後仍使用 early console？
