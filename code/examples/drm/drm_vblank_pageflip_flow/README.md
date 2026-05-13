# drm_vblank_pageflip_flow  
  
DRM vblank / page flip / display timing flow 心智模型。  
  
本章目的：  
  
> 理解 framebuffer 什麼時候真正切換到畫面上。  
  
---  
  
## 🎯 本章的目的  
  
理解：  
  
- vblank 是什麼  
- page flip 是什麼  
- tearing 為什麼發生  
- atomic commit 與 vblank 的關係  
- page flip event 在做什麼  
  
---  
  
## 🧠 一句話先記住  
  
```text  
page flip  
= scanout framebuffer 的切換
```

----------

## 🧠 vblank 是什麼？

vblank：

```
顯示控制器掃描完一整個 frame
到下一 frame 開始前的空檔
```

----------

## 🔥 為什麼 vblank 很重要？

因為：

```
只有在 vblank 切 framebuffer
才不容易 tearing
```

----------

## 🔄 scanout flow

```
CRTC scanout framebuffer A
 ↓
vblank
 ↓
切換到 framebuffer B
 ↓
下一 frame 開始 scanout
```

----------

## 🧠 tearing 是什麼？

如果：

```
scanout 到一半突然換 framebuffer
```

畫面就可能：

```
上半部是舊 frame
下半部是新 frame
```

這就是：

```
tearing
```

----------

## 🧩 Kernel 原始碼對照

```
drivers/gpu/drm/drm_vblank.c
drivers/gpu/drm/drm_atomic_helper.c
drivers/gpu/drm/drm_crtc.c
```
