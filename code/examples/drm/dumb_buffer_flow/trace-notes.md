# Kernel trace notes — dumb_buffer_flow

## 1. Level 1：用人話理解

userspace 想做的事情其實很單純：  

```text
我要一塊畫圖用的 memory  
↓  
我自己填內容  
↓  
交給 DRM 顯示
```

## 2. Level 2：流程理解

### 2.1 建立 dumb buffer

```text
DRM_IOCTL_MODE_CREATE_DUMB
```

kernel 會：

```text
配置一塊 linear framebuffer memory
```

### 2.2 mmap

```text
DRM_IOCTL_MODE_MAP_DUMB
 ↓
mmap()
```

userspace 開始能直接碰 framebuffer memory。

### 2.3 userspace 畫圖

```c
memset(buf.map, 0xff, buf.size);
```

直接改 framebuffer memory。

### 2.4 drmModeAddFB2()

這一步非常重要：

```text
buffer handle
→ DRM framebuffer object
```

從：

```text
「只是 memory」
```

變成：

```text
「DRM pipeline 可以使用的 framebuffer」
```

## 3. Level 3：kernel trace

### 3.1 create dumb

```text
DRM_IOCTL_MODE_CREATE_DUMB  
└─ drm_mode_create_dumb_ioctl  
└─ driver->dumb_create()
```

### 3.2 map dumb

```text
DRM_IOCTL_MODE_MAP_DUMB
 └─ drm_mode_mmap_dumb_ioctl
```

### 3.3 AddFB2

```text
drmModeAddFB2
 └─ DRM_IOCTL_MODE_ADDFB2
     └─ drm_mode_addfb2_ioctl
         └─ drm_internal_framebuffer_create()
```

## 4. handle vs framebuffer object

| 名稱 | 意義 |  
|---------------|-------------------|  
| GEM handle | Memory object |  
| framebuffer | Display object |

## 5. memory vs framebuffer

很多人會誤以為：  

```text
framebuffer = memory
```

但其實：

```text
memory只是像素資料（raw bytes）
```

例如：

```text
ff ff ff ff00 00 00 00...
```

memory 本身不知道：

-   寬度
-   高度
-   pixel format
-   pitch
-   怎麼顯示

### 5.1 framebuffer 是什麼？

framebuffer 是 DRM 的「顯示描述物件」。

它會描述：

```text
這塊 memory要怎麼被顯示
```

包含：

-   width
-   height
-   pixel format
-   pitch
-   GEM buffer reference

### 5.2 關鍵流程

```text
memory allocation
 ↓
GEM object / dumb buffer
 ↓
drmModeAddFB2()
 ↓
drm framebuffer object
```

### 5.3 framebuffer 真正的角色

framebuffer 的本質：

```text
metadata + memory reference
```

不是 memory 本身。

### 5.4 為什麼 DRM 要分開？

因為：

```text
同一塊 memory
可以有不同 framebuffer interpretation
```

例如：

```text
同一塊 memory
↓
RGB888 framebuffer
↓
ARGB8888 framebuffer
```

memory 沒變，  
但顯示方式不同。

## 6. 對照

| userspace | kernel |  
|----------------|--------------------|  
| dumb buffer | GEM object |  
| fb_id | drm_framebuffer |  
| mmap | GEM mmap |  
| atomic commit | pipe->update |
