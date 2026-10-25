// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static uint32_t channel(unsigned int value, struct fb_bitfield f)
{
	return f.length ? ((value * ((1U << f.length) - 1) + 127) / 255) << f.offset : 0;
}

static int valid_field(struct fb_bitfield f, unsigned int bpp)
{
	return f.length <= 8 && f.offset <= bpp &&
	       f.length <= bpp - f.offset && !f.msb_right;
}

int main(int argc, char **argv)
{
	static const unsigned int colors[] = {
		0xffffff, 0xffff00, 0x00ffff, 0x00ff00,
		0xff00ff, 0xff0000, 0x0000ff, 0x000000
	};
	static const char *const names[] = {
		"white", "yellow", "cyan", "green",
		"magenta", "red", "blue", "black"
	};
	int solid = -1;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	const char *device = argc > 1 ? argv[1] : "/dev/fb0";
	unsigned char *row;
	unsigned int bytes, x, y;
	int fd, result = EXIT_FAILURE;

	if (argc > 3) {
		fprintf(stderr, "Usage: %s [/dev/fb0 [bars|white|yellow|cyan|green|magenta|red|blue|black]]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (argc == 3 && strcmp(argv[2], "bars")) {
		for (unsigned int i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
			if (!strcmp(argv[2], names[i])) {
				solid = (int)i;
				break;
			}
		}
		if (solid < 0) {
			fprintf(stderr, "Unknown color: %s\n", argv[2]);
			return EXIT_FAILURE;
		}
	}
	fd = open(device, O_RDWR);
	if (fd < 0) {
		perror(device);
		return EXIT_FAILURE;
	}
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) < 0 ||
	    ioctl(fd, FBIOGET_VSCREENINFO, &var) < 0) {
		perror("framebuffer info");
		goto close_fd;
	}
	printf("%s: %.16s, %ux%u, %u bpp, stride %u\n", device, fix.id,
	       var.xres, var.yres, var.bits_per_pixel, fix.line_length);
	if (fix.type != FB_TYPE_PACKED_PIXELS || fix.visual != FB_VISUAL_TRUECOLOR ||
	    (var.bits_per_pixel != 16 && var.bits_per_pixel != 24 && var.bits_per_pixel != 32) ||
	    !var.xres || !var.yres || var.grayscale || var.nonstd ||
	    !valid_field(var.red, var.bits_per_pixel) ||
	    !valid_field(var.green, var.bits_per_pixel) ||
	    !valid_field(var.blue, var.bits_per_pixel) ||
	    !valid_field(var.transp, var.bits_per_pixel)) {
		fprintf(stderr, "Unsupported framebuffer format\n");
		goto close_fd;
	}
	bytes = var.bits_per_pixel / 8;
	if (((uint64_t)var.xoffset + var.xres) * bytes > fix.line_length ||
	    (uint64_t)var.yoffset + var.yres > var.yres_virtual ||
	    ((uint64_t)var.yoffset + var.yres) * fix.line_length > fix.smem_len) {
		fprintf(stderr, "Invalid framebuffer geometry\n");
		goto close_fd;
	}
	row = malloc((size_t)var.xres * bytes);
	if (!row) {
		perror("malloc");
		goto close_fd;
	}
	for (x = 0; x < var.xres; x++) {
		unsigned int rgb = colors[solid >= 0 ? (unsigned int)solid : (uint64_t)x * 8 / var.xres];
		uint32_t pixel = channel((rgb >> 16) & 255, var.red) |
			channel((rgb >> 8) & 255, var.green) |
			channel(rgb & 255, var.blue) | channel(255, var.transp);
		/* i.MX6ULL userspace is little endian. */
		for (unsigned int b = 0; b < bytes; b++)
			row[(size_t)x * bytes + b] = pixel >> (8 * b);
	}
	for (y = 0; y < var.yres; y++) {
		size_t done = 0, length = (size_t)var.xres * bytes;
		off_t offset = (off_t)(var.yoffset + y) * fix.line_length +
			      (off_t)var.xoffset * bytes;
		while (done < length) {
			ssize_t n = pwrite(fd, row + done, length - done, offset + done);
			if (n < 0 && errno == EINTR)
				continue;
			if (n <= 0) {
				if (!n)
					errno = EIO;
				perror("write framebuffer");
				goto free_row;
			}
			done += (size_t)n;
		}
	}
	if (solid >= 0)
		printf("Solid color: %s\n", names[solid]);
	else
		puts("Color bars: white yellow cyan green magenta red blue black");
	result = EXIT_SUCCESS;
free_row:
	free(row);
close_fd:
	close(fd);
	return result;
}
