# Kernel trace notes — mmap_driver_example

## 1. userspace 呼叫點
```text
mmap(NULL, size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0)
```

## 2. kernel 入口點
```text
sys_mmap / do_mmap
↓
建立 VMA (vm_area_struct)
↓
file->f_op->mmap()
↓
driver 的 .mmap()
```

## 3. 本範例 driver 的 mapping 方法

本範例採用：

- 在 kernel init 時配置一個 page（alloc_page）
- `.mmap()` 內用 `remap_pfn_range()` 把該 page 的 PFN 映射到 userspace
```text
alloc_page()
↓
page_to_pfn()
↓
remap_pfn_range(vma, user_addr, pfn, size, vma->vm_page_prot)
```

## 4. VMA / PFN 觀念

- VMA：描述 userspace 某段虛擬位址區間的「mapping 屬性」
- PFN：Page Frame Number（實體頁框編號）
- remap_pfn_range：把「指定 PFN」映射進 userspace VMA

## 5. MAP_SHARED 的意義

使用 MAP_SHARED：

- userspace 寫入 mapping
- kernel 讀同一塊頁面
- 兩邊看到的是同一份內容（共享）

## 6. 常見問題與排查（常見誤解）

✗ mmap 一定是 zero-copy（不一定，視後端）
✓ mmap 是「地址映射」，能否 zero-copy 取決於後端 buffer 來源/同步策略
