# drm_fence_sync_flow

DRM / GPU synchronization 與 dma_fence flow 心智模型。

本章目的：

> 理解 GPU render 與 DRM scanout 之間如何同步。

理解：

- fence 是什麼
- dma_fence 是什麼
- explicit sync vs implicit sync
- acquire fence / release fence
- 為什麼 atomic commit 需要 IN_FENCE_FD

## 1. 一句話先記住

```text
fence
= 「這份工作完成了嗎？」的同步機制
```

## 2. 為什麼 graphics stack 一定需要 sync？

因為：

```text
GPU render
與
display scanout

是並行進行的
```

## 3. 沒同步會怎樣？

可能：

```text
display controller 已開始 scanout
但 GPU 還沒畫完
```

結果：

-   artifact
-   tearing
-   corruption
-   半張 frame

## 4. 真正 graphics flow

```text
GPU render
 ↓
fence
 ↓
DRM atomic commit
 ↓
wait fence signal
 ↓
vblank
 ↓
page flip
```

## 5. dma_fence 是什麼？

```text
dma_fence
= kernel 內部 synchronization object
```

它代表：

```text
某個 producer 的工作完成狀態
```

## 6. sync_file 是什麼？

```text
sync_file
= userspace 可傳遞的 fence fd
```

也就是：

```text
dma_fence
↓
包裝成 fd
```

## 7. Kernel 原始碼對照

```text
drivers/dma-buf/dma-fence.c
drivers/dma-buf/sync_file.c
drivers/gpu/drm/drm_atomic.c
include/linux/dma-fence.h
```
