// SPDX-License-Identifier: MIT
/*
 * page_flip_notes.c
 *
 * Minimal notes-style userspace example for DRM page flip flow.
 */

#include <stdio.h>

int main(void)
{
	printf("DRM page flip mental model\n");
	printf("\n");

	printf("Typical rendering loop:\n");
	printf("\n");

	printf("  render framebuffer B\n");
	printf("      while CRTC scans framebuffer A\n");

	printf("\n");

	printf("  atomic commit / page flip request\n");

	printf("\n");

	printf("  wait for vblank\n");

	printf("\n");

	printf("  CRTC scanout switches:\n");
	printf("      framebuffer A -> framebuffer B\n");

	printf("\n");

	printf("Important idea:\n");
	printf("  rendering and scanout happen concurrently.\n");

	printf("\n");

	printf("Without vblank synchronization:\n");
	printf("  tearing may occur.\n");

	return 0;
}
