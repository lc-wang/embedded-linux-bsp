
# page_fault_example

Linux kernel page fault + mmap + vm_ops 最小範例。

本章的核心目標：

> **讓你親眼看到 page fault 如何觸發 driver 來提供 memory。**

---

## 🎯 本章的目的

理解：

- mmap 並不一定立刻建立 mapping
- page fault 如何觸發
- vm_area_struct / vm_operations_struct
- .fault callback 的角色

---

## 🧠 核心概念
E2 (remap_pfn_range)  
= 一次性 mapping

E3 (this chapter)  
= lazy mapping（缺頁才補）

  
---  
  
## 🔄 流程  
```
userspace access memory  
↓  
page not present  
↓  
page fault  
↓  
kernel handle_mm_fault()  
↓  
vma->vm_ops->fault()  
↓  
driver 提供 page  
↓  
mapping 建立
```
  
---  
  
## 🧩 Kernel 原始碼對照  
```
mm/memory.c  
mm/mmap.c  
include/linux/mm.h
```
