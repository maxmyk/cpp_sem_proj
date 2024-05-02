#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <signal.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

int fb_fd = 0;
void *fb_map = NULL;
size_t fb_size = 0;
int drm_fd = 0;
void *drm_map = NULL;
size_t drm_size = 0;
int drm_pitch = 0;
int drm_width = 0;
int drm_height = 0;

struct fb_var_screeninfo vinfo;

uint16_t rgb888_to_rgb565(uint8_t red, uint8_t green, uint8_t blue) {
    uint16_t r = (red >> 3) << 11;
    uint16_t g = (green >> 2) << 5;
    uint16_t b = blue >> 3;
    return r | g | b;
}

void cleanup(int sig) {
    if (drm_map) {
        munmap(drm_map, drm_size);
    }
    if (fb_map) {
        munmap(fb_map, fb_size);
    }
    if (fb_fd > 0) {
        close(fb_fd);
    }
    if (drm_fd > 0) {
        close(drm_fd);
    }
    exit(0);
}

void update_framebuffer() {
    uint8_t *src = (uint8_t *)drm_map;  // Pointer to source buffer (RGB888)
    uint16_t *dst = (uint16_t *)fb_map; // Pointer to destination buffer (RGB565)

    int visibleWidthBytesSrc = drm_width * 3;

    for (int y = 0; y < drm_height; y++) {
        for (int x = 0; x < drm_width; x++) {
            int src_index = y * drm_pitch + x * 3;
            int dst_index = y * drm_width + x;
            if (src_index < y * drm_pitch + visibleWidthBytesSrc) {
                uint8_t red   = src[src_index + 0];
                uint8_t green = src[src_index + 1];
                uint8_t blue  = src[src_index + 2];
                dst[dst_index] = rgb888_to_rgb565(red, green, blue);
            }
        }
    }
}

int main() {
    signal(SIGINT, cleanup);

    drm_fd = open("/dev/dri/card0", O_RDWR);
    if (drm_fd < 0) {
        perror("Failed to open DRM device");
        return -1;
    }

    drmModeRes *resources = drmModeGetResources(drm_fd);
    if (!resources) {
        perror("Failed to get DRM resources");
        close(drm_fd);
        return -1;
    }

    drmModeCrtc *crtc = drmModeGetCrtc(drm_fd, resources->crtcs[3]);
    if (!crtc) {
        perror("Failed to get CRTC");
        drmModeFreeResources(resources);
        close(drm_fd);
        return -1;
    }

    drmModeFB *fb = drmModeGetFB(drm_fd, crtc->buffer_id);
    if (!fb) {
        perror("Failed to get framebuffer");
        drmModeFreeCrtc(crtc);
        drmModeFreeResources(resources);
        close(drm_fd);
        return -1;
    }

    struct drm_mode_map_dumb mreq;
    memset(&mreq, 0, sizeof(mreq));
    mreq.handle = fb->handle;

    if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
        perror("Failed to map dumb buffer");
        drmModeFreeFB(fb);
        drmModeFreeCrtc(crtc);
        drmModeFreeResources(resources);
        close(drm_fd);
        return -1;
    }

    drm_pitch = fb->pitch;
    drm_width = fb->width;
    drm_height = fb->height;
    drm_size = drm_pitch * drm_height;
    drm_map = mmap(0, drm_size, PROT_READ, MAP_SHARED, drm_fd, mreq.offset);
    if (drm_map == MAP_FAILED) {
        perror("Failed to mmap DRM framebuffer");
        drmModeFreeFB(fb);
        drmModeFreeCrtc(crtc);
        drmModeFreeResources(resources);
        close(drm_fd);
        return -1;
    }

    fb_fd = open("/dev/fb0", O_RDWR);
    if (fb_fd < 0) {
        perror("Failed to open /dev/fb0");
        close(drm_fd);
        return -1;
    }

    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information from /dev/fb0");
        close(fb_fd);
        close(drm_fd);
        return -1;
    }

    fb_size = vinfo.yres_virtual * vinfo.xres_virtual * (vinfo.bits_per_pixel / 8);
    fb_map = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fb_map == MAP_FAILED) {
        perror("Failed to map framebuffer device to memory");
        close(fb_fd);
        close(drm_fd);
        return -1;
    }

    while (1) {
        update_framebuffer();
        usleep(33333); // Update at ~30 FPS
    }

    return 0;
}
