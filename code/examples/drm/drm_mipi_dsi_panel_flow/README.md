
# drm_mipi_dsi_panel_flow  
  
DRM MIPI DSI panel driver 最小範例。  
  
本章目的：  
  
> 理解一個 MIPI DSI panel driver 如何接到 DRM pipeline。  
  
---  
  
## 🎯 本章的目的  
  
理解：  
  
- `mipi_dsi_driver` 是什麼  
- `drm_panel` 是什麼  
- panel `prepare / enable / disable / unprepare` 的生命週期  
- DSI panel driver 如何 attach 到 DSI host  
- panel driver 在 DRM pipeline 中的位置  
  
---  
  
## 🧠 一句話先記住  
  
```text  
MIPI DSI panel driver  
= DSI peripheral driver + drm_panel
```

----------

## 🔄 基本架構

```
DRM encoder / bridge
 ↓
MIPI DSI host
 ↓
MIPI DSI peripheral
 ↓
panel driver
 ↓
LCD panel
```

----------

## 🧠 panel driver 負責什麼？

panel driver 通常負責：

-   reset GPIO
-   regulator power
-   init command sequence
-   sleep out
-   display on
-   mode 提供
-   backlight 控制
