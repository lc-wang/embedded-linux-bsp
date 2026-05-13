// SPDX-License-Identifier: MIT
/*
 * Minimal DRM/KMS page flip example
 *
 * Flow:
 *   create two dumb buffers
 *   set CRTC to buffer 0
 *   render into buffer 1
 *   drmModePageFlip()
 *   wait for vblank event
 *   swap front/back buffer
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

#define DRM_DEV "/dev/dri/card0"
#define NUM_BUFS 2

struct dumb_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
	uint8_t *map;
	uint32_t fb_id;
};

struct app {
	int fd;
	uint32_t conn_id;
	uint32_t crtc_id;
	drmModeModeInfo mode;
	struct dumb_buffer bufs[NUM_BUFS];
	int front;
	int back;
	bool page_flip_pending;
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
		perror("CREATE_DUMB");
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
		perror("MAP_DUMB");
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

static void destroy_dumb_buffer(int fd, struct dumb_buffer *buf)
{
	struct drm_mode_destroy_dumb dreq = {0};

	if (!buf->handle)
		return;

	if (buf->map && buf->map != MAP_FAILED)
		munmap(buf->map, buf->size);

	if (buf->fb_id)
		drmModeRmFB(fd, buf->fb_id);

	dreq.handle = buf->handle;
	ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);

	memset(buf, 0, sizeof(*buf));
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

static int find_connector_and_crtc(struct app *app)
{
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	int i;

	res = drmModeGetResources(app->fd);
	if (!res) {
		perror("drmModeGetResources");
		return -1;
	}

	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(app->fd, res->connectors[i]);
		if (!conn)
			continue;

		if (conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0) {
			app->conn_id = conn->connector_id;
			app->mode = conn->modes[0];
			break;
		}

		drmModeFreeConnector(conn);
		conn = NULL;
	}

	if (!conn) {
		fprintf(stderr, "no connected connector found\n");
		drmModeFreeResources(res);
		return -1;
	}

	if (conn->encoder_id)
		enc = drmModeGetEncoder(app->fd, conn->encoder_id);

	if (!enc) {
		fprintf(stderr, "no encoder found\n");
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return -1;
	}

	app->crtc_id = enc->crtc_id;

	printf("connector=%u crtc=%u mode=%s %ux%u\n",
	       app->conn_id,
	       app->crtc_id,
	       app->mode.name,
	       app->mode.hdisplay,
	       app->mode.vdisplay);

	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	return 0;
}

static void page_flip_handler(int fd,
			      unsigned int frame,
			      unsigned int sec,
			      unsigned int usec,
			      void *data)
{
	struct app *app = data;
	int tmp;

	printf("page flip done: frame=%u time=%u.%06u\n",
	       frame, sec, usec);

	app->page_flip_pending = false;

	tmp = app->front;
	app->front = app->back;
	app->back = tmp;
}

static int wait_page_flip_event(struct app *app)
{
	drmEventContext ev = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};

	while (app->page_flip_pending) {
		fd_set fds;
		int ret;

		FD_ZERO(&fds);
		FD_SET(app->fd, &fds);

		ret = select(app->fd + 1, &fds, NULL, NULL, NULL);
		if (ret < 0) {
			if (errno == EINTR)
				continue;

			perror("select");
			return -1;
		}

		if (FD_ISSET(app->fd, &fds)) {
			ret = drmHandleEvent(app->fd, &ev);
			if (ret) {
				fprintf(stderr, "drmHandleEvent failed: %d\n", ret);
				return -1;
			}
		}
	}

	return 0;
}

int main(void)
{
	struct app app = {0};
	uint32_t colors[] = {
		0x00ff0000, /* red */
		0x0000ff00, /* green */
		0x000000ff, /* blue */
		0x00ffffff, /* white */
	};
	int i;

	app.fd = open(DRM_DEV, O_RDWR | O_CLOEXEC);
	if (app.fd < 0) {
		perror("open DRM device");
		return 1;
	}

	if (find_connector_and_crtc(&app))
		goto err_close;

	for (i = 0; i < NUM_BUFS; i++) {
		if (create_dumb_buffer(app.fd,
				       app.mode.hdisplay,
				       app.mode.vdisplay,
				       &app.bufs[i]))
			goto err_destroy;
	}

	app.front = 0;
	app.back = 1;

	fill_color(&app.bufs[app.front],
		   app.mode.hdisplay,
		   app.mode.vdisplay,
		   colors[0]);

	if (drmModeSetCrtc(app.fd,
			   app.crtc_id,
			   app.bufs[app.front].fb_id,
			   0, 0,
			   &app.conn_id,
			   1,
			   &app.mode)) {
		perror("drmModeSetCrtc");
		goto err_destroy;
	}

	printf("initial modeset done\n");

	for (i = 1; i < 20; i++) {
		fill_color(&app.bufs[app.back],
			   app.mode.hdisplay,
			   app.mode.vdisplay,
			   colors[i % 4]);

		printf("request page flip: fb_id=%u\n",
		       app.bufs[app.back].fb_id);

		app.page_flip_pending = true;

		if (drmModePageFlip(app.fd,
				    app.crtc_id,
				    app.bufs[app.back].fb_id,
				    DRM_MODE_PAGE_FLIP_EVENT,
				    &app)) {
			perror("drmModePageFlip");
			goto err_destroy;
		}

		if (wait_page_flip_event(&app))
			goto err_destroy;
	}

	printf("done\n");

err_destroy:
	for (i = 0; i < NUM_BUFS; i++)
		destroy_dumb_buffer(app.fd, &app.bufs[i]);

err_close:
	close(app.fd);
	return 0;
}
