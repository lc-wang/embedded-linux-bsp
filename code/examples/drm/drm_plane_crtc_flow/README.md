
# drm_plane_crtc_flow

DRM plane / CRTC / scanout flow 心智模型。

本章目的：

> 理解 framebuffer 是如何真正變成「畫面輸出」。

---

## 🎯 本章的目的

理解：

- plane 是什麼
- CRTC 是什麼
- framebuffer 為什麼不會自己顯示
- atomic commit 到底在改什麼

---

## 🧠 先記住

```text
framebuffer = 一張圖

plane = 放圖的圖層

CRTC = 負責掃描輸出的核心
```

## 🔄 最核心流程

```
framebuffer
 ↓
plane
 ↓
CRTC
 ↓
scanout
 ↓
connector
 ↓
panel / monitor
```

----------

## 🧠 直覺理解

### framebuffer

```
只有像素資料
```

它不會自己出現在畫面上。

----------

### plane

```
決定：「哪張 framebuffer 要被顯示」
```

----------

### CRTC

```
負責真正掃描輸出
```

也就是：

```
一行一行把像素送到顯示硬體
```

----------

## 🧠 最重要觀念

```
framebuffer 不等於畫面

plane 綁定 framebuffer
CRTC 掃描 plane
```

----------

## 🧩 Kernel 原始碼對照

```
drivers/gpu/drm/drm_plane.c
drivers/gpu/drm/drm_crtc.c
drivers/gpu/drm/drm_atomic.c
```
