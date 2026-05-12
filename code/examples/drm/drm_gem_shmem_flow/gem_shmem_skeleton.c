// SPDX-License-Identifier: GPL-2.0
/*
 * Minimal DRM GEM shmem skeleton
 *
 * This file is a mental-model skeleton for understanding how
 * DRM GEM shmem helpers are wired into a simple DRM driver.
 *
 * It is not a complete display driver.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

#include <drm/drm_drv.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_file.h>
#include <drm/drm_prime.h>

#define DRV_NAME "gem_shmem_skel"

struct my_drm_device {
	struct drm_device drm;
};

DEFINE_DRM_GEM_FOPS(my_drm_fops);

static const struct drm_driver my_drm_driver = {
	.driver_features = DRIVER_GEM,

	/*
	 * File operations:
	 *
	 * open("/dev/dri/cardX")
	 * mmap()
	 * ioctl()
	 *
	 * all enter DRM core through these fops.
	 */
	.fops = &my_drm_fops,

	/*
	 * GEM shmem helpers:
	 *
	 * These provide:
	 * - GEM object creation helpers
	 * - mmap support
	 * - PRIME import/export helpers
	 * - vmap/vunmap helpers
	 */
	DRM_GEM_SHMEM_DRIVER_OPS,

	.name = DRV_NAME,
	.desc = "Minimal DRM GEM shmem skeleton",
	.date = "2026",
	.major = 1,
	.minor = 0,
};

static int my_probe(struct platform_device *pdev)
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

	/*
	 * Register DRM device.
	 *
	 * After this, userspace can open:
	 *
	 *   /dev/dri/cardX
	 */
	ret = drm_dev_register(drm, 0);
	if (ret)
		return ret;

	dev_info(&pdev->dev, "%s: registered\n", DRV_NAME);
	return 0;
}

static void my_remove(struct platform_device *pdev)
{
	struct my_drm_device *priv = platform_get_drvdata(pdev);

	drm_dev_unplug(&priv->drm);

	dev_info(&pdev->dev, "%s: removed\n", DRV_NAME);
}

static const struct of_device_id my_of_match[] = {
	{ .compatible = "example,gem-shmem-skeleton" },
	{ }
};
MODULE_DEVICE_TABLE(of, my_of_match);

static struct platform_driver my_platform_driver = {
	.probe = my_probe,
	.remove_new = my_remove,
	.driver = {
		.name = DRV_NAME,
		.of_match_table = my_of_match,
	},
};

module_platform_driver(my_platform_driver);

MODULE_AUTHOR("lcwang");
MODULE_DESCRIPTION("Minimal DRM GEM shmem skeleton");
MODULE_LICENSE("GPL");
