#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

struct dumb_buffer {
	uint32_t handle;
	uint32_t pitch;
	uint64_t size;
	void *map;
	uint32_t fb_id;
};

int main(void)
{
	int fd;
	struct drm_mode_create_dumb creq = {0};
	struct drm_mode_map_dumb mreq = {0};
	struct dumb_buffer buf;

	fd = open("/dev/dri/card0", O_RDWR);
	if (fd < 0) {
		perror("open");
		return 1;
	}

	/* -------------------------------------------------- */
	/* create dumb buffer                                 */
	/* -------------------------------------------------- */

	creq.width = 800;
	creq.height = 480;
	creq.bpp = 32;

	if (ioctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
		perror("CREATE_DUMB");
		return 1;
	}

	buf.handle = creq.handle;
	buf.pitch = creq.pitch;
	buf.size = creq.size;

	printf("dumb buffer created:\n");
	printf("  handle=%u\n", buf.handle);
	printf("  pitch=%u\n", buf.pitch);
	printf("  size=%llu\n",
	       (unsigned long long)buf.size);

	/* -------------------------------------------------- */
	/* prepare mmap                                       */
	/* -------------------------------------------------- */

	mreq.handle = buf.handle;

	if (ioctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
		perror("MAP_DUMB");
		return 1;
	}

	buf.map = mmap(0,
		       buf.size,
		       PROT_READ | PROT_WRITE,
		       MAP_SHARED,
		       fd,
		       mreq.offset);

	if (buf.map == MAP_FAILED) {
		perror("mmap");
		return 1;
	}

	/* -------------------------------------------------- */
	/* fill framebuffer                                   */
	/* -------------------------------------------------- */

	memset(buf.map, 0xff, buf.size);

	printf("framebuffer filled with white\n");

	/* -------------------------------------------------- */
	/* create DRM framebuffer object                      */
	/* -------------------------------------------------- */

	uint32_t handles[4] = { buf.handle };
	uint32_t pitches[4] = { buf.pitch };
	uint32_t offsets[4] = { 0 };

	if (drmModeAddFB2(fd,
			  800,
			  480,
			  DRM_FORMAT_XRGB8888,
			  handles,
			  pitches,
			  offsets,
			  &buf.fb_id,
			  0)) {
		perror("drmModeAddFB2");
		return 1;
	}

	printf("fb_id=%u created\n", buf.fb_id);

	/*
	 * 真正的程式接下來會：
	 * - 找 connector / crtc
	 * - atomic commit
	 * - 顯示到畫面
	 */

	munmap(buf.map, buf.size);
	close(fd);

	return 0;
}
