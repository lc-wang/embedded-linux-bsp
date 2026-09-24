# Linux Kernel Memory Management 概要

這份筆記說明 Linux Kernel 記憶體管理的基礎概念，  
包括 **頁面管理 (page allocator)**、**虛擬位址空間**、**kmalloc/vmalloc 差異**、  
以及 **user-space mmap 與 DMA 記憶體映射機制**。  
理解這些是撰寫驅動與除錯 memory leak 的基礎。

## 1. 記憶體層級架構

```text
User Space (Virtual Memory)
↑ mmap / brk
Kernel Space
↑ kmalloc / vmalloc / alloc_pages
Physical Memory
↑ page allocator / buddy system
```

| 層級 | 常用 API | 說明 |
| --- | --- | --- |
| **User space** | `malloc()`, `mmap()` | 透過系統呼叫分配虛擬記憶體區域。 |
| **Kernel space** | `kmalloc()`, `vmalloc()`, `alloc_pages()` | 內核分配記憶體的主要介面。 |
| **Physical memory** | page allocator (Buddy System) | 管理實體頁框 (page frame)。 |

## 2. Page Allocator 與 Buddy System

### 2.1 Buddy System 基本原理

Linux 將實體記憶體以 **4KB Page** 為單位管理，  
當需要分配連續實體記憶體時，使用 **2ⁿ 頁合併** 的方式進行。

| 函式 | 功能 | 備註 |
| --- | --- | --- |
| `alloc_pages(gfp_t gfp_mask, unsigned int order)` | 分配 `2^order` 個連續 page | 用於 DMA 或高效能緩衝 |
| `__free_pages(struct page *page, unsigned int order)` | 釋放對應頁區 | 對應 alloc_pages 使用 |
| `__get_free_page()` | 分配單一 page | 常用於驅動暫存區 |

### 2.2 GFP 標誌 (分配行為)

| 標誌 | 意義 |
| --- | --- |
| `GFP_KERNEL` | 一般情況下的分配，可睡眠 |
| `GFP_ATOMIC` | 中斷或不可睡眠環境使用 |
| `GFP_DMA` | 分配可供 DMA 使用的區域 |
| `GFP_HIGHUSER` | 分配可供 user-space 使用的高端記憶體頁 |

## 3. kmalloc vs vmalloc

| 函式 | 特性 | 物理連續性 | 用途 |
| --- | --- | --- | --- |
| **kmalloc()** | 快速分配小記憶體 | ✓ 物理連續 | 小型緩衝區、結構體 |
| **vmalloc()** | 分配大區塊記憶體 | ✗ 不保證物理連續 | 大型緩衝區、映射 I/O |
| **alloc_pages()** | 直接操作頁框 | ✓ 物理連續 | DMA / frame buffer |
| **kzalloc()** | kmalloc + memset(0) | ✓ | 初始化為 0 的分配 |

**補充：**
- `kmalloc()` 底層仍透過 **slab/slub 分配器**。  
- `vmalloc()` 使用非連續物理記憶體，但虛擬位址連續。  
- `alloc_pages()` 是更底層接口，直接與 page allocator 溝通。

## 4. User-space mmap 流程

驅動程式若提供 mmap 功能，可讓應用程式直接訪問裝置記憶體。
```text
User space mmap() call
↓
sys_mmap()
↓
do_mmap() / file_operations.mmap()
↓
remap_pfn_range() → 建立虛擬與實體頁映射
```

### 4.1 驅動範例

```c
static int mydrv_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long pfn = virt_to_phys(buffer) >> PAGE_SHIFT;
    return remap_pfn_range(vma, vma->vm_start, pfn,
                           vma->vm_end - vma->vm_start,
                           vma->vm_page_prot);
}
```
| 名稱 | 類型 | 功能說明 | 常見使用情境 |
|:--|:--|:--|:--|
| `virt_to_phys()` | 函式 | 將 **kernel 虛擬位址（virtual address）** 轉換為 **實體位址（physical address）**。 | 用於需要直接操作硬體 DMA buffer、或需傳遞實體位址給裝置時。 |
| `remap_pfn_range()` | 函式 | 將 **實體頁框（page frame）** 映射到 **使用者空間（user space）**。<br>通常在 `mmap()` 實作中使用，讓 user process 能存取 device memory 或 DMA buffer。 | 自製 character device / DRM driver 時實作 `fops->mmap()`，例如：將 framebuffer 或 e-ink panel buffer 提供給應用程式。 |
| `vm_area_struct` | 結構體 | 描述 **使用者虛擬記憶體區段（VMA, Virtual Memory Area）**，包含該區段的起迄位址、權限、對應的 file operations 等。 | 在 `mmap()` 實作時，kernel 會提供一個指向該結構的指標，可用來設定區段屬性（如禁止 swap、cacheable / non-cacheable）。 |

## 5. DMA 與 Cache 一致性

在進行 DMA（Direct Memory Access）操作時，必須確保 **CPU 與裝置對記憶體的快取資料一致**。  
若未正確處理 cache，同一區域的資料可能在 CPU 與裝置之間不同步，導致資料錯亂。

### 5.1 常用函式一覽

| 函式 | 功能 | 備註 |
|:--|:--|:--|
| `dma_alloc_coherent()` | 分配可供 DMA 使用且 cache 一致的記憶體。 | 適用於需要持續 DMA 的 buffer，例如 frame buffer。 |
| `dma_map_single()` / `dma_unmap_single()` | 將一般記憶體 buffer 映射至 DMA 位址（並在傳輸後解除映射）。 | 適用於臨時性 DMA 傳輸。 |
| `dma_sync_single_for_cpu()` | 在裝置完成寫入後，確保資料對 CPU 可見（cache flush）。 | 在 CPU 讀取前呼叫。 |
| `dma_sync_single_for_device()` | 在 CPU 寫入後，確保資料對裝置可見（cache invalidate）。 | 在 DMA 傳輸前呼叫。 |

### 5.2 範例：DMA 傳輸流程

```c
void *buf;
dma_addr_t dma_handle;

// 分配 DMA 一致性記憶體
buf = dma_alloc_coherent(dev, BUF_SIZE, &dma_handle, GFP_KERNEL);

// 裝置進行 DMA 寫入後
dma_sync_single_for_cpu(dev, dma_handle, BUF_SIZE, DMA_FROM_DEVICE);
// CPU 讀取資料
process_data(buf);
// 再次給裝置使用前
dma_sync_single_for_device(dev, dma_handle, BUF_SIZE, DMA_TO_DEVICE);
```

## 6. Debug 與觀察工具

在進行記憶體問題分析（如洩漏、fragmentation、異常分配）時，可利用以下 Kernel 內建的觀察工具與節點。

### 6.1 常用工具與節點

| 工具 / 節點 | 功能說明 |
|:--|:--|
| `/proc/meminfo` | 顯示整體系統記憶體統計資訊（總量、可用、buffers、cached 等）。 |
| `/proc/slabinfo` | 顯示 slab/slub 分配器的使用狀況與快取統計。 |
| `/sys/kernel/debug/page_owner` | 追蹤每一頁記憶體的分配來源（需開啟 `CONFIG_PAGE_OWNER`）。 |
| `/proc/vmallocinfo` | 顯示經由 `vmalloc` 配置的虛擬記憶體區域與對應模組。 |
| `kmemleak` | Kernel 記憶體洩漏偵測工具，可模擬 garbage collector 行為偵測未釋放物件。 |
| `ftrace` / `perf` | 追蹤與分析記憶體相關函式呼叫行為與效能熱點。 |

### 6.2 啟用與使用範例

**1. 啟用 page_owner**

```bash
echo 1 > /sys/kernel/debug/page_owner
cat /sys/kernel/debug/page_owner | grep my_driver
```
**2. 檢查 kmemleak 結果**
```bash
# 啟用
echo scan > /sys/kernel/debug/kmemleak
# 檢視洩漏報告
cat /sys/kernel/debug/kmemleak
```
**3. 追蹤記憶體函式呼叫**
```bash
echo function > /sys/kernel/debug/tracing/current_tracer
echo 'kmalloc*' > /sys/kernel/debug/tracing/set_ftrace_filter
cat /sys/kernel/debug/tracing/trace_pipe
```

## 7. 常見問題與排查

以下列出在 Linux Kernel 記憶體與 DMA 開發中常見的問題、可能原因與對應解決方式。

| 問題 | 可能原因 | 建議處理方式 |
|:--|:--|:--|
| **kmalloc 回傳 NULL** | 記憶體碎片過多或使用錯誤的 `GFP` 標誌 | 改用 `vmalloc()`（適合大區塊），或檢查 `GFP_KERNEL` / `GFP_ATOMIC` 使用情境。 |
| **DMA 傳輸資料錯誤** | CPU cache 與裝置未同步 | 使用 `dma_sync_single_for_cpu()` / `dma_sync_single_for_device()` 確保一致性。 |
| **mmap crash** | PFN 錯誤或位址未對齊 | 驗證 `remap_pfn_range()` 輸入參數是否正確，頁面是否以 `PAGE_SIZE` 對齊。 |
| **Kernel OOM (Out of Memory)** | 記憶體分配過度或無法回收 | 透過 `/proc/meminfo` 或 `dmesg` 分析記憶體使用狀況，確認是否有 leak。 |
| **page fault** | 存取無效虛擬位址 | 確認指標合法性、頁面是否被釋放或未映射到實體記憶體。 |

### 7.1 附註

- 若懷疑 **memory leak**，可啟用 `CONFIG_DEBUG_KMEMLEAK`。
- 若懷疑 **fragmentation**，可觀察 `/proc/pagetypeinfo` 與 `buddyinfo`。
- 若懷疑 **DMA mapping 問題**，建議開啟 `CONFIG_DMA_API_DEBUG` 以追蹤錯誤映射。

## 附錄

**延伸閱讀**

-   `Documentation/core-api/memory-allocation.rst`
-   `Documentation/vm/`   
-   LWN: The page allocator in Linux
-   `mm/page_alloc.c`, `mm/vmalloc.c`
