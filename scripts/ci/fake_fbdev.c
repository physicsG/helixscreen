// SPDX-License-Identifier: GPL-3.0-or-later
//
// LD_PRELOAD stand-in for a framebuffer driver. A container has no /dev/fb0, so
// scripts/ci/gpu_libs_hidden_smoke.sh puts a regular file there, sized to hold
// the pixels; stat(), open() and mmap() work on it as they are. This answers the
// ioctls a driver would, for that file only, and passes every other ioctl on.
//
// Build: cc -shared -fPIC -o fake_fbdev.so fake_fbdev.c -ldl
// Env:   FAKE_FBDEV names the file (default /dev/fb0). It is an 800x480
//        32bpp display, so the file must hold 800 * 480 * 4 bytes.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

enum { kXres = 800, kYres = 480, kBytesPerPixel = 4 };

static int is_fake_fb(int fd) {
    const char* path = getenv("FAKE_FBDEV");
    if (path == NULL || path[0] == '\0') {
        path = "/dev/fb0";
    }
    struct stat opened;
    struct stat named;
    return fstat(fd, &opened) == 0 && stat(path, &named) == 0 && S_ISREG(opened.st_mode) &&
           opened.st_dev == named.st_dev && opened.st_ino == named.st_ino;
}

static void fill_var(struct fb_var_screeninfo* var) {
    memset(var, 0, sizeof(*var));
    var->xres = var->xres_virtual = kXres;
    var->yres = var->yres_virtual = kYres;
    var->bits_per_pixel = kBytesPerPixel * 8;
    var->red = (struct fb_bitfield){16, 8, 0};
    var->green = (struct fb_bitfield){8, 8, 0};
    var->blue = (struct fb_bitfield){0, 8, 0};
    var->transp = (struct fb_bitfield){24, 8, 0};
}

static void fill_fix(struct fb_fix_screeninfo* fix) {
    memset(fix, 0, sizeof(*fix));
    strncpy(fix->id, "fake_fbdev", sizeof(fix->id) - 1);
    fix->smem_len = kXres * kYres * kBytesPerPixel;
    fix->line_length = kXres * kBytesPerPixel;
    fix->type = FB_TYPE_PACKED_PIXELS;
    fix->visual = FB_VISUAL_TRUECOLOR;
}

int ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    va_start(ap, request);
    void* arg = va_arg(ap, void*);
    va_end(ap);

    if (is_fake_fb(fd)) {
        switch (request) {
        case FBIOGET_VSCREENINFO:
            fill_var((struct fb_var_screeninfo*)arg);
            return 0;
        case FBIOGET_FSCREENINFO:
            fill_fix((struct fb_fix_screeninfo*)arg);
            return 0;
        case FBIOPUT_VSCREENINFO:
            fill_var((struct fb_var_screeninfo*)arg);
            return 0;
        case FBIOPAN_DISPLAY:
        case FBIOBLANK:
            return 0;
        default:
            errno = ENOTTY;
            return -1;
        }
    }

    static int (*real_ioctl)(int, unsigned long, ...);
    if (real_ioctl == NULL) {
        real_ioctl = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
    }
    return real_ioctl(fd, request, arg);
}
