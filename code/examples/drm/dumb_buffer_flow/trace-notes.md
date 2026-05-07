# Kernel trace notes — dumb_buffer_flow  
  
# 🟢 Level 1：用人話理解  
  
userspace 想做的事情其實很單純：  
  
```text  
我要一塊畫圖用的 memory  
↓  
我自己填內容  
↓  
交給 DRM 顯示
```

----------

# 🟡 Level 2：流程理解

## 1. 建立 dumb buffer

```
DRM_IOCTL_MODE_CREATE_DUMB
```

kernel 會：

```
配置一塊 linear framebuffer memory
```

----------

## 2. mmap

```
DRM_IOCTL_MODE_MAP_DUMB
 ↓
mmap()
```

userspace 開始能直接碰 framebuffer memory。

----------

## 3. userspace 畫圖

```
memset(buf.map, 0xff, buf.size);
```

👉 直接改 framebuffer memory。

----------

## 4. drmModeAddFB2()

這一步非常重要：

```
buffer handle
→ DRM framebuffer object
```

從：

```
「只是 memory」
```

變成：

```
「DRM pipeline 可以使用的 framebuffer」
```

----------

# 🔴 Level 3：kernel trace

## create dumb

```
DRM_IOCTL_MODE_CREATE_DUMB  
└─ drm_mode_create_dumb_ioctl  
└─ driver->dumb_create()
```

----------

## map dumb

```
DRM_IOCTL_MODE_MAP_DUMB
 └─ drm_mode_mmap_dumb_ioctl
```

----------

## AddFB2

```
drmModeAddFB2
 └─ DRM_IOCTL_MODE_ADDFB2
     └─ drm_mode_addfb2_ioctl
         └─ drm_internal_framebuffer_create()
```

----------

# 🧠 handle vs framebuffer object

| 名稱 | 意義 |  
|---------------|-------------------|  
| GEM handle | Memory object |  
| framebuffer | Display object |

----------

# 🧠 memory vs framebuffer
  
很多人會誤以為：  
  
```text  
framebuffer = memory
```

但其實：

```
memory只是像素資料（raw bytes）
```

例如：

```
ff ff ff ff00 00 00 00...
```

memory 本身不知道：

-   寬度
-   高度
-   pixel format
-   pitch
-   怎麼顯示

----------

## framebuffer 是什麼？

framebuffer 是 DRM 的「顯示描述物件」。

它會描述：

```
這塊 memory要怎麼被顯示
```

包含：

-   width
-   height
-   pixel format
-   pitch
-   GEM buffer reference

----------

## 🔥 關鍵流程

```
memory allocation
 ↓
GEM object / dumb buffer
 ↓
drmModeAddFB2()
 ↓
drm framebuffer object
```

----------

## 🧠 framebuffer 真正的角色

framebuffer 的本質：

```
metadata + memory reference
```

不是 memory 本身。

----------

## 🧠 為什麼 DRM 要分開？

因為：

```
同一塊 memory
可以有不同 framebuffer interpretation
```

例如：

```
同一塊 memory
↓
RGB888 framebuffer
↓
ARGB8888 framebuffer
```

memory 沒變，  
但顯示方式不同。

----------


# 🔥 對照

| userspace | kernel |  
|----------------|--------------------|  
| dumb buffer | GEM object |  
| fb_id | drm_framebuffer |  
| mmap | GEM mmap |  
| atomic commit | pipe->update |
