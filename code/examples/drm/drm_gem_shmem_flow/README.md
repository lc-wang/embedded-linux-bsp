
# drm_gem_shmem_flow

DRM GEM / shmem framebuffer memory flow 心智模型。

本章目的：

> 理解 framebuffer 底下真正的 memory object 是什麼。

---

## 🎯 本章的目的

理解：

- GEM object 是什麼
- shmem GEM 為什麼存在
- framebuffer 如何 reference GEM memory
- mmap 為什麼能看到 framebuffer memory
- dumb buffer 與 GEM 的關係

---

## 🧠 一句話先記住

```text
framebuffer 不擁有 memory

真正擁有 memory 的
通常是 GEM object
```

## 🔄 DRM memory stack

```
userspace mmap
 ↓
drm framebuffer
 ↓
GEM object
 ↓
shmem / dma memory
 ↓
physical pages
```

----------

## 🧠 GEM 是什麼？

GEM：

```
Graphics Execution Manager
```

本質上：

```
DRM 的 memory object 管理系統
```

----------

## 🧠 shmem GEM 是什麼？

shmem GEM：

```
使用 shared memory backend 的 GEM object
```

也就是：

```
memory 由 kernel shmem subsystem 提供
```

----------

## 🔥 為什麼 tiny/simple DRM driver 很常用 shmem？

因為：

```
CPU 能直接存取
mmap 容易
helper 完整
適合 simple display
```

----------

## 🧩 Kernel 原始碼對照

```
drivers/gpu/drm/drm_gem.c
drivers/gpu/drm/drm_gem_shmem_helper.c
drivers/gpu/drm/drm_framebuffer.c
mm/shmem.c
```
