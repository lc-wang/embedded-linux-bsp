# Embedded Linux BSP

> Structured notes and case studies for Embedded Linux, Android BSP, and kernel subsystems.

----------

## Overview

This repository contains:

-   Structured notes for Linux kernel and subsystems
-   BSP bring-up related knowledge
-   Android system notes
-   Driver examples with trace notes
-   Real-world debugging case studies

----------

## Contents

### Notes

[`notes/`](notes/) is organized by topic:

-   [`linux/`](notes/linux/) – Linux subsystem overviews (memory, scheduler, networking, VFS, DRM/KMS, tracing, etc.)
-   [`kernel/`](notes/kernel/) – Kernel concepts (interrupts, synchronization, memory, power management)
-   [`bsp/`](notes/bsp/) – BSP topics (boot flow, device tree, interfaces, U-Boot, Yocto, debug tools)
-   [`android/`](notes/android/) – Android system components (binder, services, boot flow, graphics, media)
-   [`firmware/`](notes/firmware/) – Firmware notes ([TF-A](notes/firmware/trusted-firmware-a/) boot flow and runtime)
-   [`subsystems/`](notes/subsystems/) – Subsystem deep dives:
    -   [`alsa/`](notes/subsystems/alsa/) – ALSA / ASoC, DAPM, machine driver
    -   [`gpio/`](notes/subsystems/gpio/) – gpiolib, device tree binding, libgpiod
    -   [`interrupt/`](notes/subsystems/interrupt/) – GIC, irq_domain, GPIO interrupts
    -   [`multimedia/`](notes/subsystems/multimedia/) – GStreamer, V4L2, zero-copy dma-buf
    -   [`networking/`](notes/subsystems/networking/) – [Ethernet](notes/subsystems/networking/ethernet/) and [switch / DSA](notes/subsystems/networking/switch-dsa/)
    -   [`security/`](notes/subsystems/security/platform-security/) – TrustZone, OP-TEE, secure boot, TPM

----------

### Case Studies

[`cases-study/`](cases-study/) contains debugging and analysis records, including:

-   Boot and flashing issues
-   Peripheral bring-up problems
-   Kernel / driver debugging
-   System configuration issues

Platforms include:

-   [NXP](cases-study/nxp/)
-   [Renesas](cases-study/renesas/)
-   [Rockchip](cases-study/rockchip/)
-   [Raspberry Pi](cases-study/raspberry-pi/)
-   [Generic Linux](cases-study/linux/)

----------

### Code Examples

[`code/examples/`](code/examples/) contains small kernel and userspace examples:

-   [`kernel/`](code/examples/kernel/) – module basics, character device, miscdevice
-   [`memory/`](code/examples/memory/) – mmap, DMA allocation, kmalloc vs vmalloc, page fault
-   [`synchronization/`](code/examples/synchronization/) – spinlock, mutex, completion, waitqueue
-   [`userspace/`](code/examples/userspace/) – ioctl
-   [`bus/`](code/examples/bus/) – platform bus
-   [`drm/`](code/examples/drm/) – DRM/KMS flows (dumb buffer, plane/CRTC, atomic commit, vblank/page flip, dma-buf PRIME, fences, GEM shmem, MIPI-DSI panel)
-   [`security/`](code/examples/security/) – minimal hash chain

Each example includes:

-   source code
-   Makefile (in the example directory, or in its `kernel/` / `userspace/` subdirectory)
-   trace notes

[`drm_fence_sync_flow`](code/examples/drm/drm_fence_sync_flow/) is notes only (no source code).

----------

### Driver Notes

-   [`bcmdhd/`](bcmdhd/) – Broadcom WiFi driver notes
-   [`bluetooth/`](bluetooth/) – Bluetooth stack and transport notes
-   [`mt76/`](mt76/) – MT76 driver analysis

----------

### Tools

-   [`tools/generate_index.js`](tools/generate_index.js) – generates [`assets/notes_index.json`](assets/notes_index.json) (run by CI on push)

----------

## Purpose

This repository is used to:

-   Organize subsystem knowledge
-   Record debugging experience
-   Keep trace notes for kernel and drivers

----------

## Author

LC Wang
