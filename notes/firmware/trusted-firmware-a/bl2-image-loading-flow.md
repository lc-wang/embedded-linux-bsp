# Trusted Firmware-A BL2 Image Loading 流程解析

在 Trusted Firmware-A（TF-A）的整體架構中，  
**BL2（Boot Loader stage 2）是整個系統中「最像 loader」的一個階段**。

BL2 的核心責任不是 runtime service，也不是 power management，而是：

- 建立可用的記憶體環境
- 載入後續 firmware images
- 驗證 image（若啟用安全機制）
- 準備並交付控制權給 BL31 / BL33

若將 TF-A 各 stage 的角色簡化：

| Stage | 主要角色 |
|------|---------|
| BL1 | Minimal ROM loader（optional） |
| **BL2** | **Image loader + memory builder** |
| BL31 | EL3 runtime firmware |
| BL33 | Normal world bootloader（U-Boot） |

本章將專注於 **BL2 的 image loading 流程與內部運作模型**。

## 1. BL2 在 Boot Flow 中的位置

整體 boot flow 中，BL2 的位置如下：
```
Boot ROM
│
▼
BL1 (optional)
│
▼
BL2
│ ← 本章重點
│
▼
BL31 (EL3 runtime)
│
▼
BL33 (U-Boot)
```

在進入 BL2 時，系統狀態通常為：

- CPU 已在 secure world
- DRAM 可能尚未完全初始化
- MMU 尚未或剛準備啟用
- 早期 console 可能已可用

## 2. BL2 的核心責任總覽

BL2 的主要工作可歸納為五大類：

1. 初始化基本記憶體環境
2. 建立 MMU 與 page table
3. 載入 firmware images
4. 驗證 image（若啟用）
5. 將 image 資訊傳遞給 BL31

其中 **image loading** 是 BL2 最關鍵的任務。

## 3. BL2 的 Image 概念模型

在 TF-A 中，每一個 firmware image 都被視為一個邏輯物件：

- BL31
- BL32（TEE，optional）
- BL33（U-Boot）

BL2 並不關心 image 內容是什麼「程式」，  
它只關心：

- image ID
- load address
- entry point
- size
- security attributes

## 4. Image Descriptor（Image 描述結構）

TF-A 使用 image descriptor 來描述每個 image：

```c
typedef struct image_desc {
    unsigned int image_id;
    image_info_t image_info;
    entry_point_info_t ep_info;
} image_desc_t;
```
概念上可以理解為：

```c
image_desc = {
    image_id,
    image memory information,
    image entry point information
}
```
BL2 的工作就是把這些 descriptor 填好。

## 5. BL2 Image Loading 的高階流程

BL2 的 image loading 流程可概括如下：
```
bl2_main()
  │
  ├─ early platform setup
  │
  ├─ memory & MMU initialization
  │
  ├─ load images
  │     ├─ BL31
  │     ├─ BL32 (optional)
  │     └─ BL33
  │
  ├─ populate entry point info
  │
  └─ handoff to BL31
```

## 6. Image Loading 的實際行為

### 6.1 Image 來源

BL2 並不直接實作 storage driver，  
實際 image 來源取決於 platform：

-   SPI flash

-   eMMC / SD

-   NOR / NAND

-   memory-mapped storage

平台需提供對應的 IO layer 實作。

### 6.2 IO 抽象層（概念）

BL2 透過 TF-A 的 IO abstraction layer 存取 image：
```
BL2
 └─ IO layer
      └─ platform storage driver
```

因此 BL2 image loading 流程本身 **與 storage 類型無關**。

## 7. 為何 BL2 要先建立 MMU？

Image loading 過程通常涉及：

-   DRAM 存取

-   複製 image 到指定 load address

-   設定 memory attribute（secure / non-secure）

這些行為在多數平台上都需要：

-   cache

-   device memory 區分

-   memory protection

因此 BL2 在正式 load image 前，  
會先建立 **自己的 MMU 與 page table**。

## 8. Image Load Address 與 Memory Layout

BL2 必須確保：

-   image load address 不重疊

-   secure / non-secure memory 區隔正確

-   BL31 位於 secure memory

-   BL33 位於 non-secure memory

錯誤的 memory layout 會導致：

-   BL31 覆蓋 BL2

-   BL33 覆蓋 page table

-   系統在跳轉後立即 exception

## 9. Entry Point Information

對 BL2 而言，image loading 的最終目標是建立：

`entry_point_info_t` 

其中包含：

-   entry point address

-   CPU execution state

-   security state

-   SPSR 設定

BL2 **不會直接跳到 BL33**，而是：

> 將 BL31 與 BL33 的 entry point 資訊交給 BL31。

## 10. BL2 與 BL31 的責任分界

這是一個常被誤解的地方：

-   BL2：負責 **載入 image**

-   BL31：負責 **決定何時跳轉**

流程如下：
```
BL2
 └─ load BL31 + BL33
 └─ prepare entry info
 └─ jump to BL31
        │
        └─ BL31 later jumps to BL33
```

BL2 在完成 image loading 後，其任務即結束。

## 11. 為何 BL2 不直接跳 BL33？

原因包括：

-   BL31 必須先建立 EL3 runtime

-   Secure Monitor（SMC）需初始化

-   Power state / PSCI 尚未準備好

因此設計上：

> **BL31 是所有非 secure world 執行的必經之路。**
