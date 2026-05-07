
# Kernel trace notes — drm_simple_kms
  
# 🟢 Level 1：用人話理解  
  
DRM driver 做的事情其實很單純：  
  
```text  
有人給我一張圖  
↓  
我決定怎麼顯示  
↓  
最後把它送到輸出裝置
```

在 simple KMS 模型裡，這條路徑被簡化成：

```
framebuffer
↓
simple display pipe
↓
connector
↓
display
```

----------

# 🟡 Level 2：流程理解

## 1. driver probe

```
platform_driver probe
 ↓
建立 drm_device
 ↓
建立 connector
 ↓
建立 drm_simple_display_pipe
 ↓
drm_dev_register()
```

這一步做完後，DRM 裝置才真正出現在系統裡。

----------

## 2. userspace 建 framebuffer

userspace 可能透過：

```
fbdev emulation
或
DRM ioctl（dumb buffer / AddFB2 / atomic commit）
```

建立一張 framebuffer。

----------

## 3. atomic commit

當顯示內容要更新時：

```
userspace request
 ↓
drm atomic framework
 ↓
helper commit flow
 ↓
driver callback
```

在 `drm_simple_display_pipe` 裡，最重要的 callback 是：

-   `enable`
-   `update`
-   `disable`

----------

## 4. 真正更新畫面的位置

這一章最重要的觀念：

```
driver 真正碰硬體的地方
通常就在 pipe->update()
```

也就是：

```
framebuffer 已經準備好了
現在要把它送進實際顯示硬體
```

----------

# 🔴 Level 3：kernel trace

## probe 路徑

```
platform probe
 └─ my_drm_probe()
     ├─ devm_drm_dev_alloc()
     ├─ drmm_mode_config_init()
     ├─ drm_connector_init()
     ├─ drm_simple_display_pipe_init()
     ├─ drm_mode_config_reset()
     ├─ drm_dev_register()
     └─ drm_fbdev_generic_setup()
```
----------

## 顯示更新路徑

```
drm_mode_atomic_ioctl
 └─ drm_atomic_commit
     └─ drm_atomic_helper_commit
         └─ drm_atomic_helper_commit_planes
             └─ pipe->update()
```

----------

## 如果第一次開啟顯示

```
atomic commit
 └─ pipe->enable()
```

----------

## 如果關閉輸出

```
atomic commit
 └─ pipe->disable()
```

----------

# 🧠 framebuffer / plane / CRTC / connector 的關係

| 元件 | 直覺理解 |  
|-------------|--------------------|  
| framebuffer | 一張圖 |  
| plane | 放圖的圖層 |  
| CRTC | 掃描輸出的核心 |  
| connector | 對外輸出端口 |

在 `drm_simple_display_pipe` 模型裡，  
這些關係被打包得比較簡單，  
適合先建立基本心智模型。

----------

# 🧠 為什麼 simple display pipe 很常見？

因為很多小型顯示裝置其實不需要：

-   多 plane
-   複雜 overlay
-   多組輸出拓樸
-   高階 color pipeline

這時候用 `drm_simple_display_pipe`：

```
簡單好維護足夠對應單一路徑顯示裝置
```
