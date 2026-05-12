// SPDX-License-Identifier: MIT
/*
 * Minimal DRM/KMS modeset example
 *
 * Flow:
 *   open DRM card
 *   find connected connector
 *   find encoder / CRTC
 *   create dumb buffer
 *   mmap buffer
 *   fill color
 *   drmModeAddFB
 *   drmModeSetCrtc
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>

#define DRM_DEV "/dev/dri/card0"

struct dumb_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
	uint8_t *map;
	uint32_t fb_id;
};

static int create_dumb_buffer(int fd, uint32_t width, uint32_t height,
			      struct dumb_buffer *buf)
{
	struct drm_mode_create_dumb creq = {0};
	struct drm_mode_map_dumb mreq = {0};

	creq.width = width;
	creq.height = height;
	creq.bpp = 32;

	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		perror("DRM_IOCTL_MODE_CREATE_DUMB");
		return -1;
	}

	buf->handle = creq.handle;
	buf->pitch = creq.pitch;
	buf->size = creq.size;

	if (drmModeAddFB(fd, width, height, 24, 32,
			 buf->pitch, buf->handle, &buf->fb_id)) {
		perror("drmModeAddFB");
		return -1;
	}

	mreq.handle = buf->handle;

	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		perror("DRM_IOCTL_MODE_MAP_DUMB");
		return -1;
	}

	buf->map = mmap(NULL, buf->size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED, fd, mreq.offset);
	if (buf->map == MAP_FAILED) {
		perror("mmap");
		return -1;
	}

	return 0;
}

static void fill_color(struct dumb_buffer *buf, uint32_t width,
		       uint32_t height, uint32_t color)
{
	uint32_t x, y;

	for (y = 0; y < height; y++) {
		uint32_t *row = (uint32_t *)(buf->map + y * buf->pitch);

		for (x = 0; x < width; x++)
			row[x] = color;
	}
}

int main(void)
{
	int fd;
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	drmModeModeInfo mode;
	struct dumb_buffer buf = {0};
	uint32_t conn_id = 0;
	uint32_t crtc_id = 0;
	int i;

	fd = open(DRM_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open DRM device");
		return 1;
	}

	res = drmModeGetResources(fd);
	if (!res) {
		perror("drmModeGetResources");
		close(fd);
		return 1;
	}

	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;

		if (conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0) {
			conn_id = conn->connector_id;
			mode = conn->modes[0];
			break;
		}

		drmModeFreeConnector(conn);
		conn = NULL;
	}

	if (!conn) {
		fprintf(stderr, "no connected connector found\n");
		drmModeFreeResources(res);
		close(fd);
		return 1;
	}

	printf("connector=%u mode=%s %ux%u\n",
	       conn_id, mode.name, mode.hdisplay, mode.vdisplay);

	if (conn->encoder_id)
		enc = drmModeGetEncoder(fd, conn->encoder_id);

	if (!enc) {
		fprintf(stderr, "no encoder found\n");
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		close(fd);
		return 1;
	}

	crtc_id = enc->crtc_id;

	printf("encoder=%u crtc=%u\n", enc->encoder_id, crtc_id);

	if (create_dumb_buffer(fd, mode.hdisplay, mode.vdisplay, &buf)) {
		fprintf(stderr, "failed to create dumb buffer\n");
		drmModeFreeEncoder(enc);
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		close(fd);
		return 1;
	}

	/* XRGB8888: 0x00RRGGBB */
	fill_color(&buf, mode.hdisplay, mode.vdisplay, 0x00ff0000);

	printf("fb_id=%u handle=%u pitch=%u size=%llu\n",
	       buf.fb_id, buf.handle, buf.pitch,
	       (unsigned long long)buf.size);

	if (drmModeSetCrtc(fd, crtc_id, buf.fb_id,
			   0, 0, &conn_id, 1, &mode)) {
		perror("drmModeSetCrtc");
		return 1;
	}

	printf("screen should be red now. press Enter to exit...\n");
	getchar();

	munmap(buf.map, buf.size);
	drmModeRmFB(fd, buf.fb_id);

	{
		struct drm_mode_destroy_dumb dreq = {0};
		dreq.handle = buf.handle;
		ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	}

	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	close(fd);

	return 0;
}
