# Kernel trace notes — drm_gem_shmem_flow


# 🟢 Level 1：用人話理解

framebuffer 看起來像：

```text
一張圖
```

但：

```
framebuffer 本身其實沒有真正的像素資料
```

真正的像素資料：

```
在 GEM memory object 裡
```

----------

# 🟢 GEM object 是什麼？

你可以把 GEM object 想成：

```
DRM 專用的 memory container
```

它負責：

-   管理 memory
-   提供 mmap
-   reference counting
-   share/import/export

----------

# 🟡 Level 2：流程理解

## dumb buffer flow

```
CREATE_DUMB
 ↓
建立 GEM object
 ↓
配置 memory
 ↓
回傳 handle
```

----------

## AddFB2

```
drmModeAddFB2()
```

會做：

```
framebuffer
↓
reference GEM object
```

----------

# 🔥 最重要觀念

```
framebuffer
只是「如何顯示」

GEM object
才是真正的 memory owner
```

----------

# 🟡 mmap flow

```
userspace mmap
 ↓
GEM mmap handler
 ↓
vm_area_struct
 ↓
page fault
 ↓
shmem page
```



# 🔴 Level 3：kernel trace


## 1. userspace 建立 dumb buffer  
``` 
userspace：  
DRM_IOCTL_MODE_CREATE_DUMB
↓
kernel：
drm_mode_create_dumb_ioctl()
↓
driver：
driver->dumb_create()
```

通常 simple DRM driver 會：

```
drm_gem_shmem_create()
```

## 🧠 drm_gem_shmem_create 做了什麼？

它會：

```
1. 建立 GEM object
2. 配置 backing memory
3. 建立 shmem mapping
4. 回傳 GEM handle
```

也就是：

```
GEM object真正開始擁有 memory
```

----------

## 2. userspace mmap framebuffer memory
```
userspace：
mmap()
↓
kernel：
drm_gem_mmap()
↓
helper：
drm_gem_shmem_mmap()
```

這一步：

```
建立 VMA（虛擬記憶體區間）
但 page 還不一定存在
```

----------

## 3. userspace 第一次 access memory

例如：

```
p[0] = 0xff;
↓
CPU 發現 page table 沒有 mapping
↓
page fault
↓
kernel：
drm_gem_shmem_fault()
```

----------

## 🧠 drm_gem_shmem_fault 做了什麼？

它會：

```
1. 找到對應 shmem page
2. 如果 page 不存在 → 配置 page
3. 建立 userspace mapping
4. 回傳給 MM subsystem
```

----------

## 🔥 所以真正流程其實是

```
userspace mmap
 ↓
VMA 建立
 ↓
第一次 access
 ↓
page fault
 ↓
drm_gem_shmem_fault()
 ↓
shmem page 出現
 ↓
userspace 真正拿到 memory
```

----------

## 🧠 最重要理解

```
mmap()
不代表 page 已存在

page fault
才是真正拿到 page 的時刻
```

----------

# 🧠 GEM vs framebuffer


| 元件 | 本質 |  
|---------------|-------------------------------|  
| GEM object | Memory owner |  
| framebuffer | Display metadata |  
| plane | 顯示哪張 framebuffer |  
| CRTC | Scanout |

----------

# 🧠為什麼同一塊 GEM memory 可以共用？

想成：

```
一塊共享畫布
```

----------

不同人：

-   GPU
-   Wayland compositor
-   DRM display

都可以：

```
看同一塊畫布
```

----------

# 🔥 不是 copy！

這很重要：

❌ GPU render 一份  
❌ compositor copy 一份  
❌ DRM 再 copy 一份

----------

真正 modern graphics stack：

```
同一塊 memory
一路傳下去
```

----------

# 🟡 真實流程

## 1️⃣ GPU render

GPU：

```
把畫面畫進 GEM memory
```

例如：

```
OpenGL render target
```

----------

## 2️⃣ Wayland compositor

Wayland：

```
拿到同一塊 memory
```

做：

-   composition
-   scaling
-   layer management

----------

## 3️⃣ DRM scanout

最後：

```
DRM plane 指向同一塊 memory
```

CRTC：

```
直接 scanout
```

----------

# 🔥 所以真正發生的是

```
同一塊 memory：

GPU 寫
↓
Wayland 管理
↓
DRM 顯示
```

----------

# 🧠 為什麼這很重要？

因為：

```
copy framebuffer 超貴
```

尤其：

-   4K
-   60fps
-   多 layer

----------

所以 modern graphics stack：

```
核心目標 = zero-copy
```

----------

# 🔴 kernel 世界

這通常透過：

```
dma-buf
```

完成。

----------

## GPU driver

輸出：

```
dma-buf fd
```

----------

## Wayland / compositor

拿到：

```
dma-buf fd
```

import 成自己的 GEM object。

----------

## DRM driver

再：

```
drm_gem_prime_import()
```

得到：

```
scanout-able GEM memory
```

----------

# 🧠 所以真正的是

```
同一塊 physical memory
```

被：

-   GPU
-   compositor
-   DRM

共同 reference。

# 🔧 kernel skeleton 對照程式本章新增：

```text
gem_shmem_skeleton.c
```

它不是完整 display driver，而是用來觀察：

```
DRM driver
 ↓
GEM shmem helper
 ↓
mmap / PRIME / GEM object
```


## 🧠 這個 skeleton 的重點

```
DRM_GEM_SHMEM_DRIVER_OPS
```

這一行會幫 driver 接上 GEM shmem helper。

它提供：

-   GEM memory object 管理
-   mmap 支援
-   PRIME import/export 基礎
-   vmap / vunmap helper

----------

## 🔴 對應 kernel flow

```
userspace open /dev/dri/cardX
 ↓
DRM fops
 ↓
DRM ioctl / mmap
 ↓
GEM shmem helper
```
