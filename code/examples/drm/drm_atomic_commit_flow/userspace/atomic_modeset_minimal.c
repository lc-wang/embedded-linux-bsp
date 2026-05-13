// SPDX-License-Identifier: MIT
/*
 * Minimal DRM atomic modeset example
 *
 * Flow:
 *   open DRM card
 *   enable atomic client capability
 *   find connector / CRTC / primary plane
 *   create dumb buffer
 *   create framebuffer
 *   create MODE_ID blob
 *   atomic commit:
 *     connector CRTC_ID
 *     CRTC MODE_ID + ACTIVE
 *     plane FB_ID + CRTC_ID + SRC/CRTC rectangles
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

#define DRM_DEV "/dev/dri/card0"

struct dumb_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
	uint8_t *map;
	uint32_t fb_id;
};

struct object_props {
	uint32_t crtc_id;
	uint32_t mode_id;
	uint32_t active;

	uint32_t fb_id;
	uint32_t src_x;
	uint32_t src_y;
	uint32_t src_w;
	uint32_t src_h;
	uint32_t crtc_x;
	uint32_t crtc_y;
	uint32_t crtc_w;
	uint32_t crtc_h;
};

static uint32_t get_prop_id(int fd, uint32_t obj_id,
			    uint32_t obj_type, const char *name)
{
	drmModeObjectProperties *props;
	drmModePropertyRes *prop;
	uint32_t id = 0;
	uint32_t i;

	props = drmModeObjectGetProperties(fd, obj_id, obj_type);
	if (!props)
		return 0;

	for (i = 0; i < props->count_props; i++) {
		prop = drmModeGetProperty(fd, props->props[i]);
		if (!prop)
			continue;

		if (!strcmp(prop->name, name))
			id = prop->prop_id;

		drmModeFreeProperty(prop);

		if (id)
			break;
	}

	drmModeFreeObjectProperties(props);
	return id;
}

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

static void fill_color(struct dumb_buffer *buf,
		       uint32_t width, uint32_t height,
		       uint32_t color)
{
	uint32_t x, y;

	for (y = 0; y < height; y++) {
		uint32_t *row = (uint32_t *)(buf->map + y * buf->pitch);

		for (x = 0; x < width; x++)
			row[x] = color;
	}
}

static void destroy_dumb_buffer(int fd, struct dumb_buffer *buf)
{
	struct drm_mode_destroy_dumb dreq = {0};

	if (buf->map && buf->map != MAP_FAILED)
		munmap(buf->map, buf->size);

	if (buf->fb_id)
		drmModeRmFB(fd, buf->fb_id);

	if (buf->handle) {
		dreq.handle = buf->handle;
		ioctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
	}
}

static int find_connector_crtc(int fd,
			       uint32_t *conn_id,
			       uint32_t *crtc_id,
			       drmModeModeInfo *mode)
{
	drmModeRes *res;
	drmModeConnector *conn = NULL;
	drmModeEncoder *enc = NULL;
	int i;

	res = drmModeGetResources(fd);
	if (!res) {
		perror("drmModeGetResources");
		return -1;
	}

	for (i = 0; i < res->count_connectors; i++) {
		conn = drmModeGetConnector(fd, res->connectors[i]);
		if (!conn)
			continue;

		if (conn->connection == DRM_MODE_CONNECTED &&
		    conn->count_modes > 0) {
			*conn_id = conn->connector_id;
			*mode = conn->modes[0];
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
		enc = drmModeGetEncoder(fd, conn->encoder_id);

	if (!enc) {
		fprintf(stderr, "no encoder found\n");
		drmModeFreeConnector(conn);
		drmModeFreeResources(res);
		return -1;
	}

	*crtc_id = enc->crtc_id;

	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);

	return 0;
}

static int find_primary_plane(int fd, uint32_t crtc_id, uint32_t *plane_id)
{
	drmModePlaneRes *planes;
	drmModePlane *plane;
	drmModeRes *res;
	uint32_t crtc_index = 0;
	int i, j;

	res = drmModeGetResources(fd);
	if (!res)
		return -1;

	for (i = 0; i < res->count_crtcs; i++) {
		if (res->crtcs[i] == crtc_id) {
			crtc_index = i;
			break;
		}
	}

	if (i == res->count_crtcs) {
		fprintf(stderr, "failed to find crtc index\n");
		drmModeFreeResources(res);
		return -1;
	}

	planes = drmModeGetPlaneResources(fd);
	if (!planes) {
		perror("drmModeGetPlaneResources");
		drmModeFreeResources(res);
		return -1;
	}

	for (i = 0; i < planes->count_planes; i++) {
		uint32_t type_id;
		drmModeObjectProperties *props;

		plane = drmModeGetPlane(fd, planes->planes[i]);
		if (!plane)
			continue;

		if (!(plane->possible_crtcs & (1 << crtc_index))) {
			drmModeFreePlane(plane);
			continue;
		}

		type_id = get_prop_id(fd, plane->plane_id,
				      DRM_MODE_OBJECT_PLANE, "type");
		props = drmModeObjectGetProperties(fd, plane->plane_id,
						   DRM_MODE_OBJECT_PLANE);
		if (!props) {
			drmModeFreePlane(plane);
			continue;
		}

		for (j = 0; j < props->count_props; j++) {
			if (props->props[j] == type_id &&
			    props->prop_values[j] == DRM_PLANE_TYPE_PRIMARY) {
				*plane_id = plane->plane_id;

				drmModeFreeObjectProperties(props);
				drmModeFreePlane(plane);
				drmModeFreePlaneResources(planes);
				drmModeFreeResources(res);
				return 0;
			}
		}

		drmModeFreeObjectProperties(props);
		drmModeFreePlane(plane);
	}

	drmModeFreePlaneResources(planes);
	drmModeFreeResources(res);

	fprintf(stderr, "no primary plane found\n");
	return -1;
}

static int load_props(int fd, uint32_t conn_id, uint32_t crtc_id,
		      uint32_t plane_id, struct object_props *p)
{
	p->crtc_id = get_prop_id(fd, conn_id,
				 DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID");

	p->mode_id = get_prop_id(fd, crtc_id,
				 DRM_MODE_OBJECT_CRTC, "MODE_ID");
	p->active = get_prop_id(fd, crtc_id,
				DRM_MODE_OBJECT_CRTC, "ACTIVE");

	p->fb_id = get_prop_id(fd, plane_id,
			       DRM_MODE_OBJECT_PLANE, "FB_ID");
	p->crtc_id = get_prop_id(fd, plane_id,
				 DRM_MODE_OBJECT_PLANE, "CRTC_ID");
	p->src_x = get_prop_id(fd, plane_id,
			       DRM_MODE_OBJECT_PLANE, "SRC_X");
	p->src_y = get_prop_id(fd, plane_id,
			       DRM_MODE_OBJECT_PLANE, "SRC_Y");
	p->src_w = get_prop_id(fd, plane_id,
			       DRM_MODE_OBJECT_PLANE, "SRC_W");
	p->src_h = get_prop_id(fd, plane_id,
			       DRM_MODE_OBJECT_PLANE, "SRC_H");
	p->crtc_x = get_prop_id(fd, plane_id,
				DRM_MODE_OBJECT_PLANE, "CRTC_X");
	p->crtc_y = get_prop_id(fd, plane_id,
				DRM_MODE_OBJECT_PLANE, "CRTC_Y");
	p->crtc_w = get_prop_id(fd, plane_id,
				DRM_MODE_OBJECT_PLANE, "CRTC_W");
	p->crtc_h = get_prop_id(fd, plane_id,
				DRM_MODE_OBJECT_PLANE, "CRTC_H");

	return 0;
}

int main(void)
{
	int fd;
	uint32_t conn_id = 0;
	uint32_t crtc_id = 0;
	uint32_t plane_id = 0;
	uint32_t blob_id = 0;
	drmModeModeInfo mode;
	struct dumb_buffer buf = {0};
	struct object_props props = {0};
	drmModeAtomicReq *req;
	int ret;

	fd = open(DRM_DEV, O_RDWR | O_CLOEXEC);
	if (fd < 0) {
		perror("open DRM device");
		return 1;
	}

	drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);

	if (find_connector_crtc(fd, &conn_id, &crtc_id, &mode))
		goto err_close;

	if (find_primary_plane(fd, crtc_id, &plane_id))
		goto err_close;

	printf("connector=%u crtc=%u plane=%u mode=%s %ux%u\n",
	       conn_id, crtc_id, plane_id,
	       mode.name, mode.hdisplay, mode.vdisplay);

	if (create_dumb_buffer(fd, mode.hdisplay, mode.vdisplay, &buf))
		goto err_close;

	fill_color(&buf, mode.hdisplay, mode.vdisplay, 0x0000ff00);

	if (drmModeCreatePropertyBlob(fd, &mode, sizeof(mode), &blob_id)) {
		perror("drmModeCreatePropertyBlob");
		goto err_destroy;
	}

	load_props(fd, conn_id, crtc_id, plane_id, &props);

	req = drmModeAtomicAlloc();
	if (!req) {
		fprintf(stderr, "drmModeAtomicAlloc failed\n");
		goto err_blob;
	}

	/* connector state */
	drmModeAtomicAddProperty(req, conn_id,
				 get_prop_id(fd, conn_id,
					     DRM_MODE_OBJECT_CONNECTOR,
					     "CRTC_ID"),
				 crtc_id);

	/* CRTC state */
	drmModeAtomicAddProperty(req, crtc_id, props.mode_id, blob_id);
	drmModeAtomicAddProperty(req, crtc_id, props.active, 1);

	/* plane state */
	drmModeAtomicAddProperty(req, plane_id, props.fb_id, buf.fb_id);
	drmModeAtomicAddProperty(req, plane_id, props.crtc_id, crtc_id);

	drmModeAtomicAddProperty(req, plane_id, props.src_x, 0);
	drmModeAtomicAddProperty(req, plane_id, props.src_y, 0);
	drmModeAtomicAddProperty(req, plane_id, props.src_w,
				 mode.hdisplay << 16);
	drmModeAtomicAddProperty(req, plane_id, props.src_h,
				 mode.vdisplay << 16);

	drmModeAtomicAddProperty(req, plane_id, props.crtc_x, 0);
	drmModeAtomicAddProperty(req, plane_id, props.crtc_y, 0);
	drmModeAtomicAddProperty(req, plane_id, props.crtc_w,
				 mode.hdisplay);
	drmModeAtomicAddProperty(req, plane_id, props.crtc_h,
				 mode.vdisplay);

	ret = drmModeAtomicCommit(fd, req,
				  DRM_MODE_ATOMIC_ALLOW_MODESET,
				  NULL);
	if (ret) {
		perror("drmModeAtomicCommit");
		drmModeAtomicFree(req);
		goto err_blob;
	}

	drmModeAtomicFree(req);

	printf("atomic modeset done. screen should be green.\n");
	printf("press Enter to exit...\n");
	getchar();

err_blob:
	if (blob_id)
		drmModeDestroyPropertyBlob(fd, blob_id);

err_destroy:
	destroy_dumb_buffer(fd, &buf);

err_close:
	close(fd);
	return 0;
}
