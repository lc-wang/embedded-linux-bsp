# drm_atomic_commit_flow  
  
DRM atomic commit flow 心智模型。  
  
本章目的：  
  
> 理解 modern DRM 如何一次更新整個 display pipeline state。  
  
---  
  
## 🎯 本章的目的  
  
理解：  
  
- atomic commit 是什麼  
- 為什麼 DRM 要 atomic  
- drm_atomic_state 是什麼  
- plane / CRTC / connector state 如何一起更新  
- page flip 真正發生在哪裡  
  
---  
  
## 🧠 一句話先記住  
  
```text  
atomic commit  
= 一次更新整個 display state
```

----------

## 🔥 為什麼需要 atomic？

舊世界：

```
先改 plane再改 CRTC再改 connector
```

問題：

```
中途可能畫面錯亂
```

----------

## atomic 世界

```
所有 state
一起驗證
一起切換
```

----------

## 🧠 atomic commit 更新哪些東西？

```
plane state
CRTC state
connector state
```

----------

## 🔄 最核心流程

```
userspace atomic request
 ↓
drm_atomic_state
 ↓
check
 ↓
commit
 ↓
driver callbacks
 ↓
hardware update
```

----------

## 🧠 真正重要的觀念

atomic commit 的本質：

```
不是「立刻改硬體」

而是：
「建立一份新的 display state」
```

----------

## 🧩 Kernel 原始碼對照

```
drivers/gpu/drm/drm_atomic.c
drivers/gpu/drm/drm_atomic_helper.c
drivers/gpu/drm/drm_plane.c
drivers/gpu/drm/drm_crtc.c
```
