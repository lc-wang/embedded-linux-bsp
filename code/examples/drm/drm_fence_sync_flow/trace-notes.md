# Kernel trace notes — drm_fence_sync_flow  

  
# 🟢 Level 1：用人話理解  
  
假設：  
  
```text  
GPU 正在畫 framebuffer B
```

但：

```
display controller 已經準備 scanout
```

問題：

```
GPU 到底畫完了沒？
```

----------

# 🟢 fence 是什麼？

fence：

```
「工作完成通知」
```

----------

## GPU render flow

```
GPU render start
 ↓
GPU working...
 ↓
GPU render done
 ↓
signal fence
```

----------

# 🟡 Level 2：graphics synchronization


## 沒 fence 的世界

display：

```
直接 scanout framebuffer
```

可能：

```
GPU 還沒畫完
```

結果：

-   畫面破碎
-   artifact
-   tearing

----------

## 有 fence 的世界

DRM：

```
wait GPU completion fence
```

↓

```
GPU signal done
```

↓

```
才允許 page flip
```

----------

# 🔥 acquire fence 是什麼？

```
consumer 等 producer 完成
```

例如：


| producer | consumer |  
|-----------|------------|  
| GPU | DRM |  
| camera | GPU |  
| decoder | compositor |
----------

# 🔥 release fence 是什麼？

```
consumer 通知：
buffer 已經用完
```

producer 才能安全 reuse buffer。

----------

# 🟡 explicit sync vs implicit sync

## implicit sync

kernel：

```
偷偷幫你同步
```

userspace：

```
看不到 fence
```

----------

## explicit sync

userspace：

```
明確傳 fence fd
```

例如：

```
IN_FENCE_FD
OUT_FENCE_PTR
```

----------

# 🔴 Level 3：kernel trace（真正發生什麼）


## GPU driver

GPU render：

```
submit GPU job
```

↓

建立：

```
struct dma_fence
```

----------

## signal

GPU IRQ：

```
render complete
```

↓

```
dma_fence_signal()
```

----------

## userspace

GPU driver export：

```
sync_file fd
```

userspace 拿到：

```
fence fd
```

----------

## DRM atomic commit

userspace：

```
IN_FENCE_FD = gpu_fence_fd
```

↓

kernel：

```
drm_atomic_set_fence_for_plane()
```

↓

```
dma_fence_wait()
```

----------

# 🔥 真正 page flip timing

```
GPU render done
 ↓
fence signal
 ↓
DRM atomic commit continues
 ↓
wait vblank
 ↓
page flip
```

----------

# 🧠 Android 世界

SurfaceFlinger / HWC：

```
acquire fence
↓
HWC wait
↓
display
↓
release fence
```

----------

# 🧠 Wayland 世界

Wayland compositor：

```
linux explicit sync protocol
```

也是 fence fd 傳遞。

----------

# 🧠 Vulkan 世界

Vulkan：

```
timeline semaphore
sync fd
```

最後也可能接到 dma_fence。

----------

# 🔥 最重要觀念

```
atomic commit
不只更新 framebuffer

還要確保：
「buffer 內容已經準備好」
```

----------

# 🧠 最重要一句話

```
fence 的本質：「這塊 memory 現在能安全被使用了嗎？」
```
