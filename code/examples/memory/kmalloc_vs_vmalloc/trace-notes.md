# Kernel trace notes — kmalloc_vs_vmalloc

## 1. kmalloc() 走哪裡？
```
kmalloc()
	└─ slab allocator
		└─ page allocator
```

- 使用 slab / slub
- 實體連續
- 可能失敗（高階 order）

## 2. vmalloc() 走哪裡？
```
vmalloc()
	└─ vmap()
		└─ 建立虛擬連續映射
```

- 實體不連續
- page table 組合
- 不適合 DMA

## 3. 為什麼 vmalloc 不能 DMA？

因為：

DMA 需要實體連續位址

而 vmalloc：

virt addr 連續
phys addr 不連續

## 4. 常見 driver 實例

| Driver | 使用 |
|------|------|
| DRM GEM | dma_alloc_coherent |
| netdev skb | kmalloc |
| camera buffer | CMA |
| debug buffer | vmalloc |

## 5. Context 限制

GFP_KERNEL → 可以睡眠
GFP_ATOMIC → 不能睡眠

在以下情境 **不能用 GFP_KERNEL**：

- interrupt handler
- spinlock 區段
- atomic context

## 6. 心智模型

kmalloc
= 小、快、可 DMA

vmalloc
= 大、慢、不可 DMA
