# I2S 與 Codec Driver 深度解析

> 從 snd_soc_dai_ops 到 clock 設定與 hw_params

## 1. ASoC Driver 分層回顧

之前我們講到：
```
Machine driver
CPU DAI driver
Codec driver
```
這一章我們聚焦在：
```
CPU DAI driver
Codec driver
```

## 2. CPU DAI Driver 是什麼？

CPU DAI driver 通常是：

`SoC 內建 I2S controller driver` 

例如：
```
sound/soc/rockchip/rockchip_i2s.c
sound/soc/fsl/fsl_sai.c
sound/soc/renesas/rcar/
```
它負責：

-   控制 I2S register

-   設定 clock

-   啟動 DMA

-   設定 frame format

-   設定 sample rate

## 3. 核心結構：snd_soc_dai_driver

在 CPU driver 中你會看到：
```
static  struct  snd_soc_dai_driver  rockchip_i2s_dai = {
    .name = "rockchip-i2s",
    .playback = {
        .stream_name = "Playback",
        .channels_min = 2,
        .channels_max = 2,
        .rates = SNDRV_PCM_RATE_8000_192000,
        .formats = SNDRV_PCM_FMTBIT_S16_LE,
    },
    .ops = &rockchip_i2s_dai_ops,
};
```

## 4. snd_soc_dai_ops

```
struct snd_soc_dai_ops {
    int (*startup)(...);
    void (*shutdown)(...);
    int (*hw_params)(...);
    int (*set_fmt)(...);
    int (*trigger)(...);
};
```
這是 driver 真正運作的地方。

## 5. 播放完整 call flow

播放時會發生：
```
aplay
  ↓ snd_pcm_hw_params()
  ↓ snd_soc_pcm_hw_params()
  ↓
cpu_dai->ops->hw_params()
codec_dai->ops->hw_params()
```
然後：
```
snd_soc_dai_trigger()
  ↓
cpu_dai->ops->trigger()
codec_dai->ops->trigger()
```

## 6. hw_params 在做什麼？

這是最重要的函式。

典型內容：
```
static int rockchip_i2s_hw_params(...)
{
    int rate = params_rate(params);
    int width = params_width(params);

    configure_bclk(rate, width);
    configure_lrclk(rate);
    configure_dma();
}
```
它負責：

-   設定 sample rate

-   設定 bit width

-   設定 DMA buffer

-   設定 I2S frame format

## 7. I2S Clock 計算

I2S clock 組成：
```
MCLK
  ↓
BCLK
  ↓
LRCLK
```
公式：
```
LRCLK = Sample Rate
BCLK = Sample Rate × Channels × BitWidth
```
例如：

-   48kHz

-   2 channel

-   16 bit

```text
BCLK = 48000 × 2 × 16
     = 1.536 MHz
```

## 8. Master / Slave 問題

在 DAI link 中：

`.dai_fmt = SND_SOC_DAIFMT_CBS_CFS` 

表示：

-   CPU bit clock slave

-   CPU frame slave

如果設定錯誤：

-   聲音變雜音

-   完全沒聲音

-   clock 不同步

## 9. Codec Driver 是什麼？

Codec driver 通常：

`I2C driver` 

例如：
```
wm8960.c
rt5651.c
```
它負責：

-   設定 DAC / ADC register

-   設定 mixer

-   設定 bias

-   定義 DAPM widgets

## 10. Codec Driver 結構

```
static struct snd_soc_component_driver soc_codec_dev_wm8960 = {
    .dapm_widgets = wm8960_dapm_widgets,
    .dapm_routes = wm8960_routes,
};
```
並且：
```
static struct snd_soc_dai_driver wm8960_dai = {
    .name = "wm8960-hifi",
    .ops = &wm8960_dai_ops,
};
```

## 11. codec hw_params 在做什麼？

典型：
```
static int wm8960_hw_params(...)
{
    int rate = params_rate(params);
    set_pll(rate);
    set_sysclk(rate);
}
```
Codec 可能需要：

-   設定 PLL

-   設定 internal clock divider

-   設定 oversampling

## 12. CPU DAI vs Codec DAI 誰是 clock master？

三種常見模式：

### 12.1 CPU master

CPU 提供 BCLK + LRCLK

### 12.2 Codec master

Codec 提供 BCLK

### 12.3 External clock

例如 audio PLL

## 13. 為什麼聲音會變成雜音？

通常原因：

-   sample rate mismatch

-   BCLK 設錯

-   bit width mismatch

-   codec internal PLL 設錯

這些都發生在：

`hw_params` 

## 14. 常見問題與排查

### 14.1 BSP Debug Clock Checklist

#### Step 1

`示波器量 BCLK` 

#### Step 2

`量 LRCLK` 

#### Step 3

`確認 sample rate` 

#### Step 4

`確認 dai_fmt` 

## 15. Driver 初始化流程

Probe 時：
```
platform_driver_probe
  ↓
devm_snd_soc_register_component()
  ↓
註冊 DAI
```
Codec driver：
```
i2c_probe
  ↓
snd_soc_register_component()
```

## 16. 完整播放流程

```
Machine driver 建立 link
  ↓
CPU DAI probe
Codec DAI probe
  ↓ snd_soc_register_card()
  ↓
建立 runtime
  ↓
aplay
  ↓
hw_params
  ↓
trigger
  ↓
I2S controller start DMA
  ↓
codec 開 DAC
  ↓
speaker 輸出
```

## 17. 心智模型總結

ASoC driver 成功條件：

`Machine driver 正確 + DAI link 正確 + hw_params clock 正確 + DAPM graph 正確`
