// SPDX-License-Identifier: MIT
/*
 * prime_fd_notes.c
 *
 * Minimal userspace notes for DRM PRIME dma-buf fd flow.
 *
 * This is intentionally a "flow example", not a full runnable display app.
 *
 * Real world:
 *
 *   GPU / allocator / V4L2
 *      exports dma-buf fd
 *          ↓
 *   userspace passes fd
 *          ↓
 *   DRM imports fd as GEM handle
 *          ↓
 *   drmModeAddFB2()
 *          ↓
 *   plane / CRTC scanout
 */

#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define DRM_DEV "/dev/dri/card0"

/*
 * 假設這個 fd 是別的 subsystem 給你的 dma-buf fd。
 *
 * 真實來源可能是：
 *
 * - GPU driver
 * - Android gralloc
 * - Wayland client
 * - V4L2 video decoder
 * - camera capture buffer
 */
static int get_external_dma_buf_fd(void)
{
	/*
	 * 這裡故意不實作。
	 *
	 * 因為 dma-buf fd 一定要由 producer export 出來，
	 * 不是隨便 open 一個檔案就能假裝。
	 */
	return -1;
}

int main(void)
{
	int drm_fd;
	int dma_buf_fd;
	uint32_t gem_handle;
	uint32_t fb_id;

	uint32_t width = 800;
	uint32_t height = 480;
	uint32_t pitch = width * 4;
	uint32_t offset = 0;

	uint32_t handles[4] = {0};
	uint32_t pitches[4] = {0};
	uint32_t offsets[4] = {0};

	drm_fd = open(DRM_DEV, O_RDWR | O_CLOEXEC);
	if (drm_fd < 0) {
		perror("open DRM device");
		return 1;
	}

	/*
	 * Step 1:
	 * Get dma-buf fd from producer.
	 */
	dma_buf_fd = get_external_dma_buf_fd();
	if (dma_buf_fd < 0) {
		printf("No external dma-buf fd available.\n");
		printf("\n");
		printf("This file is a notes-style example.\n");
		printf("In real systems, dma-buf fd comes from:\n");
		printf("  - Android gralloc\n");
		printf("  - GPU driver\n");
		printf("  - V4L2 exporter\n");
		printf("  - Wayland client buffer\n");
		close(drm_fd);
		return 0;
	}

	/*
	 * Step 2:
	 * Import dma-buf fd into DRM as GEM handle.
	 *
	 * Important:
	 *
	 *   dma-buf fd  -> shared memory object
	 *   GEM handle  -> DRM-local reference to that memory
	 *
	 * This does NOT copy memory.
	 */
	if (drmPrimeFDToHandle(drm_fd, dma_buf_fd, &gem_handle)) {
		perror("drmPrimeFDToHandle");
		close(dma_buf_fd);
		close(drm_fd);
		return 1;
	}

	printf("imported dma-buf fd=%d as GEM handle=%u\n",
	       dma_buf_fd, gem_handle);

	/*
	 * Step 3:
	 * Wrap imported GEM handle as DRM framebuffer.
	 *
	 * framebuffer = display metadata + GEM memory reference
	 */
	handles[0] = gem_handle;
	pitches[0] = pitch;
	offsets[0] = offset;

	if (drmModeAddFB2(drm_fd,
			  width,
			  height,
			  DRM_FORMAT_XRGB8888,
			  handles,
			  pitches,
			  offsets,
			  &fb_id,
			  0)) {
		perror("drmModeAddFB2");
		close(dma_buf_fd);
		close(drm_fd);
		return 1;
	}

	printf("created framebuffer fb_id=%u from imported GEM handle=%u\n",
	       fb_id, gem_handle);

	/*
	 * Step 4:
	 * Real program would now use atomic commit:
	 *
	 *   plane FB_ID = fb_id
	 *   CRTC_ID
	 *   connector CRTC_ID
	 *
	 * Then DRM scanout reads the same shared memory.
	 */
	printf("\n");
	printf("Next real step would be atomic commit:\n");
	printf("  imported dma-buf memory\n");
	printf("    -> GEM handle\n");
	printf("    -> framebuffer\n");
	printf("    -> plane\n");
	printf("    -> CRTC scanout\n");

	drmModeRmFB(drm_fd, fb_id);

	/*
	 * GEM handle cleanup usually requires DRM_IOCTL_GEM_CLOSE.
	 * libdrm provides drmCloseBufferHandle() on newer versions,
	 * but this example keeps cleanup minimal and explanatory.
	 */

	close(dma_buf_fd);
	close(drm_fd);

	return 0;
}
