# Kernel trace notes — drm_mipi_dsi_panel_flow

## 1. Level 1：用人話理解

MIPI DSI panel driver 不是完整 DRM driver。  

它比較像：  

```text  
「螢幕本體的驅動」
```

它負責：

```
這片 panel 要怎麼上電
怎麼 reset
怎麼送初始化命令
怎麼開畫面
怎麼關畫面
```

## 2. panel driver 在哪裡？

整體 display pipeline 大概是：

```
DRM CRTC
 ↓
encoder / bridge
 ↓
MIPI DSI host
 ↓
MIPI DSI panel driver
 ↓
panel
```

## 3. Level 2：driver 結構

一個 MIPI DSI panel driver 通常同時有兩個身份：

```
mipi_dsi_driver
+
drm_panel
```

### 3.1 mipi_dsi_driver

負責掛在 MIPI DSI bus 上：

```
compatible match
 ↓
probe()
 ↓
mipi_dsi_attach()
```

### 3.2 drm_panel

提供 DRM panel callback：

```
prepare()
enable()
disable()
unprepare()
get_modes()
```

## 4. prepare / enable 差異

### 4.1 prepare

通常做：

```
power on
reset GPIO
init sequence
exit sleep mode
```

意思是：

```
panel 硬體準備好了
```

### 4.2 enable

通常做：

```
display on
backlight on
```

意思是：

```
畫面可以亮了
```

### 4.3 disable

通常做：

```
backlight off
display off
```

### 4.4 unprepare

通常做：

```
enter sleep mode
power off
reset low
```

## 5. Level 3：kernel trace

### 5.1 Device Tree match

```
compatible = "example,panel-dsi-minimal"
↓
mipi_dsi_driver.probe()
↓
minimal_panel_probe()
```

### 5.2 probe 內部流程

```
minimal_panel_probe()
 ├─ devm_kzalloc()
 ├─ 設定 dsi->lanes / format / mode_flags
 ├─ drm_panel_init()
 ├─ drm_panel_add()
 └─ mipi_dsi_attach()
```

### 5.3 display enable flow

當 DRM pipeline 要啟用輸出時：

```
atomic commit
 ↓
bridge / encoder enable
 ↓
drm_panel_prepare()
 ↓
panel->prepare()
 ↓
drm_panel_enable()
 ↓
panel->enable()
```

### 5.4 display disable flow

關閉輸出時：

```
atomic disable
 ↓
drm_panel_disable()
 ↓
panel->disable()
 ↓
drm_panel_unprepare()
 ↓
panel->unprepare()
```

## 6. get_modes() 在做什麼？

```
get_modes()
```

負責告訴 DRM：

```
這片 panel 支援哪些解析度 / timing
```

本範例提供固定 mode：

```
800x480
```

## 7. DSI attach 是什麼？

```
mipi_dsi_attach()
```

意思是：

```
把這個 panel peripheral 接到 DSI host
```

如果沒有 attach：

```
DSI host 不知道這個 panel 存在
```

## 8. 常見問題與排查（常見誤解）

✗ panel driver 負責 framebuffer  
✗ panel driver 負責 atomic commit  
✗ panel driver 負責 GEM memory

✓ panel driver 主要負責：

```
panel power / init / mode / enable lifecycle
```

## 9. 最重要一句話

```
DRM core 管「怎麼顯示」

panel driver 管「這片螢幕怎麼被打開」
```
