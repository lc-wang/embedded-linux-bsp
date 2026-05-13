// SPDX-License-Identifier: MIT
/*
 * atomic_modeset_notes.c
 *
 * Minimal userspace notes for DRM atomic commit flow.
 *
 * This is intentionally not a full runnable compositor.
 */

#include <stdio.h>
#include <stdint.h>

#include <xf86drm.h>
#include <xf86drmMode.h>

int main(void)
{
	printf("DRM atomic commit mental model\n");
	printf("\n");

	printf("userspace prepares:\n");
	printf("  - framebuffer\n");
	printf("  - plane state\n");
	printf("  - crtc state\n");
	printf("  - connector routing\n");

	printf("\n");

	printf("Typical flow:\n");
	printf("\n");

	printf("  drmModeAtomicAlloc()\n");
	printf("    -> create atomic request object\n");

	printf("\n");

	printf("  drmModeAtomicAddProperty()\n");
	printf("    -> add FB_ID / CRTC_ID / ACTIVE / MODE_ID\n");

	printf("\n");

	printf("  drmModeAtomicCommit()\n");
	printf("    -> kernel validates full display state\n");
	printf("    -> driver updates hardware atomically\n");

	printf("\n");

	printf("Important idea:\n");
	printf("  atomic commit updates DISPLAY STATE,\n");
	printf("  not just a single framebuffer.\n");

	return 0;
}
