# kmalloc_vs_vmalloc

Linux kernel 中最基本、也最容易被誤用的記憶體配置 API 範例。

本章用來建立一個非常重要的觀念：

> **不是所有記憶體都一樣。**

理解以下差異：

- kmalloc()
- kzalloc()
- vmalloc()
- 記憶體是否連續
- 實體位址 vs 虛擬位址
- 何時可以拿去做 DMA

## 1. 快速結論

| API | 虛擬連續 | 實體連續 | 可 DMA | 常見用途 |
|----|----------|----------|--------|----------|
| kmalloc | ✓ | ✓ | ✓ | 小型 buffer |
| kzalloc | ✓ | ✓ | ✓ | driver struct |
| vmalloc | ✓ | ✗ | ✗ | 大型 buffer |
| vzalloc | ✓ | ✗ | ✗ | 大型 zeroed |

## 2. Kernel 原始碼對照
```
mm/slab.c
mm/vmalloc.c
include/linux/slab.h
```

## 3. 常見問題與排查

✗ 用 vmalloc 的 buffer 做 DMA  
✗ 假設 kmalloc 一定成功  
✗ 在 atomic context 用 GFP_KERNEL  
