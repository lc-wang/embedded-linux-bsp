// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal DRM simple KMS mental model example
 *
 * This is a teaching skeleton, not a complete production driver.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_generic.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_modes.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_simple_kms_helper.h>

#define DRV_NAME "my_simple_kms"
#define MY_XRES  800
#define MY_YRES  480

struct my_drm_device {
	struct drm_device drm;
	struct drm_simple_display_pipe pipe;
	struct drm_connector connector;
};

static inline struct my_drm_device *to_my_drm(struct drm_device *drm)
{
	return container_of(drm, struct my_drm_device, drm);
}

/* ------------------------------------------------------------------------- */
/* Fixed mode                                                                 */
/* ------------------------------------------------------------------------- */

static const struct drm_display_mode my_mode = {
	.clock = 33300,
	.hdisplay = MY_XRES,
	.hsync_start = MY_XRES + 40,
	.hsync_end = MY_XRES + 40 + 48,
	.htotal = MY_XRES + 40 + 48 + 40,
	.vdisplay = MY_YRES,
	.vsync_start = MY_YRES + 13,
	.vsync_end = MY_YRES + 13 + 3,
	.vtotal = MY_YRES + 13 + 3 + 29,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
	.name = "800x480",
};

/* ------------------------------------------------------------------------- */
/* Connector                                                                  */
/* ------------------------------------------------------------------------- */

static int my_conn_get_modes(struct drm_connector *connector)
{
	struct drm_display_mode *mode;

	mode = drm_mode_duplicate(connector->dev, &my_mode);
	if (!mode)
		return -ENOMEM;

	drm_mode_set_name(mode);
	drm_mode_probed_add(connector, mode);

	connector->display_info.width_mm = 154;
	connector->display_info.height_mm = 86;

	return 1;
}

static const struct drm_connector_helper_funcs my_conn_helper_funcs = {
	.get_modes = my_conn_get_modes,
};

static enum drm_connector_status
my_conn_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs my_conn_funcs = {
	.reset = drm_atomic_helper_connector_reset,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.detect = my_conn_detect,
	.destroy = drm_connector_cleanup,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

/* ------------------------------------------------------------------------- */
/* Simple display pipe callbacks                                              */
/* ------------------------------------------------------------------------- */

static void my_pipe_enable(struct drm_simple_display_pipe *pipe,
			   struct drm_crtc_state *crtc_state,
			   struct drm_plane_state *plane_state)
{
	pr_info("%s: pipe enable\n", DRV_NAME);
}

static void my_pipe_disable(struct drm_simple_display_pipe *pipe)
{
	pr_info("%s: pipe disable\n", DRV_NAME);
}

static void my_pipe_update(struct drm_simple_display_pipe *pipe,
			   struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = pipe->plane.state;
	struct drm_framebuffer *fb = state->fb;

	if (!fb) {
		pr_info("%s: no framebuffer\n", DRV_NAME);
		return;
	}

	pr_info("%s: pipe update fb=%ux%u pitch=%u format=%p4cc\n",
		DRV_NAME,
		fb->width,
		fb->height,
		fb->pitches[0],
		&fb->format->format);

	/*
	 * 真正的 driver 會在這裡：
	 * - 取得 framebuffer backing storage
	 * - 轉成硬體可接受的格式
	 * - 寫入 controller / panel / DMA engine
	 */
}

static const uint32_t my_formats[] = {
	DRM_FORMAT_XRGB8888,
	DRM_FORMAT_ARGB8888,
};

static const struct drm_simple_display_pipe_funcs my_pipe_funcs = {
	.enable = my_pipe_enable,
	.disable = my_pipe_disable,
	.update = my_pipe_update,
	.prepare_fb = drm_gem_simple_display_pipe_prepare_fb,
};

/* ------------------------------------------------------------------------- */
/* DRM driver                                                                 */
/* ------------------------------------------------------------------------- */

DEFINE_DRM_GEM_FOPS(my_fops);

static const struct drm_driver my_drm_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,
	.fops = &my_fops,
	.name = DRV_NAME,
	.desc = "Minimal DRM simple KMS skeleton",
	.date = "2026",
	.major = 1,
	.minor = 0,
	DRM_GEM_SHMEM_DRIVER_OPS,
};

/* ------------------------------------------------------------------------- */
/* Probe / remove                                                             */
/* ------------------------------------------------------------------------- */

static int my_drm_probe(struct platform_device *pdev)
{
	struct my_drm_device *priv;
	struct drm_device *drm;
	int ret;

	priv = devm_drm_dev_alloc(&pdev->dev, &my_drm_driver,
				  struct my_drm_device, drm);
	if (IS_ERR(priv))
		return PTR_ERR(priv);

	drm = &priv->drm;
	platform_set_drvdata(pdev, priv);

	ret = drmm_mode_config_init(drm);
	if (ret)
		return ret;

	drm->mode_config.min_width = MY_XRES;
	drm->mode_config.max_width = MY_XRES;
	drm->mode_config.min_height = MY_YRES;
	drm->mode_config.max_height = MY_YRES;

	drm_connector_helper_add(&priv->connector, &my_conn_helper_funcs);

	ret = drm_connector_init(drm, &priv->connector, &my_conn_funcs,
				 DRM_MODE_CONNECTOR_DPI);
	if (ret)
		return ret;

	ret = drm_simple_display_pipe_init(drm,
					   &priv->pipe,
					   &my_pipe_funcs,
					   my_formats,
					   ARRAY_SIZE(my_formats),
					   NULL,
					   &priv->connector);
	if (ret)
		return ret;

	drm_mode_config_reset(drm);

	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	drm_fbdev_generic_setup(drm, 32);

	pr_info("%s: probe done\n", DRV_NAME);
	return 0;
}

static void my_drm_remove(struct platform_device *pdev)
{
	struct my_drm_device *priv = platform_get_drvdata(pdev);

	drm_dev_unplug(&priv->drm);
	pr_info("%s: remove\n", DRV_NAME);
}

static const struct of_device_id my_of_match[] = {
	{ .compatible = "example,my-simple-kms" },
	{ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver my_platform_driver = {
	.probe = my_drm_probe,
	.remove_new = my_drm_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = my_of_match,
	},
};

module_platform_driver(my_platform_driver);

MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Minimal DRM simple KMS skeleton");
MODULE_LICENSE("GPL");
