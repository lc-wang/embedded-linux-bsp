// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal MIPI DSI panel driver skeleton
 *
 * This is a teaching skeleton for understanding:
 *
 *   mipi_dsi_driver
 *     + drm_panel
 *     + prepare / enable lifecycle
 *
 * It is not tied to a real panel.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/delay.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <drm/drm_modes.h>

#define DRV_NAME "panel_dsi_minimal"

struct minimal_panel {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;

	bool prepared;
	bool enabled;
};

static inline struct minimal_panel *to_minimal_panel(struct drm_panel *panel)
{
	return container_of(panel, struct minimal_panel, panel);
}

/* ------------------------------------------------------------------------- */
/* Fixed display mode                                                         */
/* ------------------------------------------------------------------------- */

static const struct drm_display_mode minimal_mode = {
	.clock = 33300,

	.hdisplay = 800,
	.hsync_start = 800 + 40,
	.hsync_end = 800 + 40 + 48,
	.htotal = 800 + 40 + 48 + 40,

	.vdisplay = 480,
	.vsync_start = 480 + 13,
	.vsync_end = 480 + 13 + 3,
	.vtotal = 480 + 13 + 3 + 29,

	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.width_mm = 154,
	.height_mm = 86,
};

/* ------------------------------------------------------------------------- */
/* Panel lifecycle                                                            */
/* ------------------------------------------------------------------------- */

static int minimal_panel_prepare(struct drm_panel *panel)
{
	struct minimal_panel *ctx = to_minimal_panel(panel);

	if (ctx->prepared)
		return 0;

	dev_info(panel->dev, "panel prepare\n");

	/*
	 * Real panel driver usually does:
	 *
	 *   regulator_enable()
	 *   reset GPIO sequence
	 *   msleep()
	 *   mipi_dsi_dcs_soft_reset()
	 *   init command sequence
	 *   mipi_dsi_dcs_exit_sleep_mode()
	 */

	msleep(120);

	ctx->prepared = true;
	return 0;
}

static int minimal_panel_enable(struct drm_panel *panel)
{
	struct minimal_panel *ctx = to_minimal_panel(panel);

	if (ctx->enabled)
		return 0;

	dev_info(panel->dev, "panel enable\n");

	/*
	 * Real panel driver usually does:
	 *
	 *   mipi_dsi_dcs_set_display_on()
	 *   backlight_enable()
	 */

	ctx->enabled = true;
	return 0;
}

static int minimal_panel_disable(struct drm_panel *panel)
{
	struct minimal_panel *ctx = to_minimal_panel(panel);

	if (!ctx->enabled)
		return 0;

	dev_info(panel->dev, "panel disable\n");

	/*
	 * Real panel driver usually does:
	 *
	 *   backlight_disable()
	 *   mipi_dsi_dcs_set_display_off()
	 */

	ctx->enabled = false;
	return 0;
}

static int minimal_panel_unprepare(struct drm_panel *panel)
{
	struct minimal_panel *ctx = to_minimal_panel(panel);

	if (!ctx->prepared)
		return 0;

	dev_info(panel->dev, "panel unprepare\n");

	/*
	 * Real panel driver usually does:
	 *
	 *   mipi_dsi_dcs_enter_sleep_mode()
	 *   reset GPIO low
	 *   regulator_disable()
	 */

	msleep(120);

	ctx->prepared = false;
	return 0;
}

static int minimal_panel_get_modes(struct drm_panel *panel,
				   struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &minimal_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = minimal_mode.width_mm;
	connector->display_info.height_mm = minimal_mode.height_mm;

	return 1;
}

static const struct drm_panel_funcs minimal_panel_funcs = {
	.prepare = minimal_panel_prepare,
	.enable = minimal_panel_enable,
	.disable = minimal_panel_disable,
	.unprepare = minimal_panel_unprepare,
	.get_modes = minimal_panel_get_modes,
};

/* ------------------------------------------------------------------------- */
/* MIPI DSI driver                                                            */
/* ------------------------------------------------------------------------- */

static int minimal_panel_probe(struct mipi_dsi_device *dsi)
{
	struct minimal_panel *ctx;
	int ret;

	ctx = devm_kzalloc(&dsi->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	/*
	 * DSI bus configuration.
	 *
	 * Real values depend on panel datasheet.
	 */
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO |
			  MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			  MIPI_DSI_MODE_LPM;

	drm_panel_init(&ctx->panel, &dsi->dev,
		       &minimal_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);

	drm_panel_add(&ctx->panel);

	/*
	 * Attach this DSI peripheral to the DSI host.
	 *
	 * After attach, the DSI host / bridge side can route display data
	 * to this panel.
	 */
	ret = mipi_dsi_attach(dsi);
	if (ret) {
		dev_err(&dsi->dev, "failed to attach DSI device: %d\n", ret);
		drm_panel_remove(&ctx->panel);
		return ret;
	}

	dev_info(&dsi->dev, "%s probed\n", DRV_NAME);
	return 0;
}

static void minimal_panel_remove(struct mipi_dsi_device *dsi)
{
	struct minimal_panel *ctx = mipi_dsi_get_drvdata(dsi);

	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	dev_info(&dsi->dev, "%s removed\n", DRV_NAME);
}

static const struct of_device_id minimal_panel_of_match[] = {
	{ .compatible = "example,panel-dsi-minimal" },
	{ }
};
MODULE_DEVICE_TABLE(of, minimal_panel_of_match);

static struct mipi_dsi_driver minimal_panel_driver = {
	.probe = minimal_panel_probe,
	.remove = minimal_panel_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = minimal_panel_of_match,
	},
};

module_mipi_dsi_driver(minimal_panel_driver);

MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Minimal MIPI DSI DRM panel skeleton");
MODULE_LICENSE("GPL");
