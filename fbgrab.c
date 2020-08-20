/*
 * fbgrab - take screenshots of the framebuffer.
 *
 * Copyright © 2020 Humm <hummsmith42@gmail.com>
 * Copyright © 2002 Gunnar Monell <gmo@linux.nu>
 * Copyright © 2000 Stephan Beyer <fbshot@s-beyer.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2.

 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.

 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * fbgrab is based on Stephan Beyer’s FBShot.
 */

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/vt.h>
#include <unistd.h>
#include <zlib.h>

#define	VERSION	"1.3.3"
#define	DEFAULT_FB	"/dev/fb0"
#define	MAX_LEN	512
#define	UNDEFINED	-1

static int	srcBlue = 0;
static int	srcGreen = 1;
static int	srcRed = 2;
static int	srcAlpha = 3;

static const int	Blue = 0;
static const int	Green = 1;
static const int	Red = 2;
static const int	Alpha = 3;

/* noreturn */ static void
fatal_error(char *message)
{
	fprintf(stderr, "%s\n", message);
	exit(EXIT_FAILURE);
}

static int
myatoi(char *s)
{
	errno = 0;
	int i = (int)strtol(s, 0, 10);
	if (errno != 0) {
		fprintf(stderr,
		        "converting string “%s” to integer failed: ", s);
		perror(0);
		exit(EXIT_FAILURE);
	}
	return i;
}

static void
usage(char *binary)
{
	fprintf(stderr, "usage: %s [ -iv ] [ -b bitdepth ] [ -c|-C console ] "
	                "[ -d device ] [ -f file ] [ -h height ] "
	                "[ -s seconds ] [ -w width ] [ -z level ] file\n",
	                binary);
	exit(EXIT_FAILURE);
}

static void
chvt(int num)
{
	int fd;

	if ((fd = open("/dev/console", O_RDWR)) == -1)
		fatal_error("Cannot open /dev/console");

	if (ioctl(fd, VT_ACTIVATE, num) != 0)
		fatal_error("ioctl VT_ACTIVATE");

	if (ioctl(fd, VT_WAITACTIVE, num) != 0)
		fatal_error("ioctl VT_WAITACTIVE");

	close(fd);
}

static unsigned short int
change_to_vt(unsigned short int vt_num)
{
	int fd;
	unsigned short int old_vt;
	struct vt_stat vt_info;

	memset(&vt_info, 0, sizeof vt_info);

	if ((fd = open("/dev/console", O_RDONLY)) == -1)
		fatal_error("Couldn't open /dev/console");

	if (ioctl(fd, VT_GETSTATE, &vt_info) != 0)
		fatal_error("ioctl VT_GETSTATE");

	close(fd);

	old_vt = vt_info.v_active;

	/* go there for information */
	chvt((int)vt_num);

	return old_vt;
}

static void
get_framebufferdata(char *device, struct fb_var_screeninfo *fb_varinfo_p,
                    struct fb_fix_screeninfo *fb_fixinfo_p, int verbose)
{
	int fd;

	/* now open framebuffer device */
	if ((fd = open(device, O_RDONLY)) == -1) {
		fprintf(stderr, "Error: Couldn't open %s.\n", device);
		exit(EXIT_FAILURE);
	}

	if (ioctl(fd, FBIOGET_VSCREENINFO, fb_varinfo_p) != 0)
		fatal_error("ioctl FBIOGET_VSCREENINFO");

	if (ioctl(fd, FBIOGET_FSCREENINFO, fb_fixinfo_p) != 0)
		fatal_error("ioctl FBIOGET_FSCREENINFO");

	if (verbose) {
		fprintf(stderr, "frame buffer fixed info:\n");
		fprintf(stderr, "id: \"%s\"\n", fb_fixinfo_p->id);
		switch (fb_fixinfo_p->type) {
		case FB_TYPE_PACKED_PIXELS:
			fprintf(stderr, "type: packed pixels\n");
			break;
		case FB_TYPE_PLANES:
			fprintf(stderr, "type: non interleaved planes\n");
			break;
		case FB_TYPE_INTERLEAVED_PLANES:
			fprintf(stderr, "type: interleaved planes\n");
			break;
		case FB_TYPE_TEXT:
			fprintf(stderr, "type: text/attributes\n");
			break;
		case FB_TYPE_VGA_PLANES:
			fprintf(stderr, "type: EGA/VGA planes\n");
			break;
		default:
			fprintf(stderr, "type: undefined!\n");
			break;
		}
		fprintf(stderr, "line length: %i bytes (%i pixels)\n",
		        fb_fixinfo_p->line_length,
		        fb_fixinfo_p->line_length
		        / (fb_varinfo_p->bits_per_pixel / 8));

		fprintf(stderr, "\nframe buffer variable info:\n");
		fprintf(stderr, "resolution: %ix%i\n", fb_varinfo_p->xres,
		        fb_varinfo_p->yres);
		fprintf(stderr, "virtual resolution: %ix%i\n",
		        fb_varinfo_p->xres_virtual,
		        fb_varinfo_p->yres_virtual);
		fprintf(stderr, "offset: %ix%i\n", fb_varinfo_p->xoffset,
		        fb_varinfo_p->yoffset);
		fprintf(stderr, "bits_per_pixel: %i\n",
		        fb_varinfo_p->bits_per_pixel);
		fprintf(stderr, "grayscale: %s\n",
		        fb_varinfo_p->grayscale ? "true" : "false");
		fprintf(stderr,
		        "red:   offset: %i, length: %i, msb_right: %i\n",
		        fb_varinfo_p->red.offset, fb_varinfo_p->red.length,
		        fb_varinfo_p->red.msb_right);
		fprintf(stderr,
		        "green: offset: %i, length: %i, msb_right: %i\n",
		        fb_varinfo_p->green.offset, fb_varinfo_p->green.length,
		        fb_varinfo_p->green.msb_right);
		fprintf(stderr,
		        "blue:  offset: %i, length: %i, msb_right: %i\n",
		        fb_varinfo_p->blue.offset, fb_varinfo_p->blue.length,
		        fb_varinfo_p->blue.msb_right);
		fprintf(stderr,
		        "alpha: offset: %i, length: %i, msb_right: %i\n",
		        fb_varinfo_p->transp.offset,
		        fb_varinfo_p->transp.length,
		        fb_varinfo_p->transp.msb_right);
		fprintf(stderr, "pixel format: %s\n",
		        fb_varinfo_p->nonstd == 0 ? "standard"
		                                  : "non-standard");
	}
	srcBlue = fb_varinfo_p->blue.offset >> 3;
	srcGreen = fb_varinfo_p->green.offset >> 3;
	srcRed = fb_varinfo_p->red.offset >> 3;

	if (fb_varinfo_p->transp.length > 0)
		srcAlpha = fb_varinfo_p->transp.offset >> 3;
	else
		srcAlpha = -1; /* not used */

	close(fd);
}

static void
read_framebuffer(char *device, size_t bytes, unsigned char *buf_p,
                 int skip_bytes)
{
	int fd;

	if ((fd = open(device, O_RDONLY)) == -1) {
		fprintf(stderr, "Error: Couldn't open %s.\n", device);
		exit(EXIT_FAILURE);
	}

	if (skip_bytes)
		lseek(fd, skip_bytes, SEEK_SET);

	if (buf_p == NULL || read(fd, buf_p, bytes) != (ssize_t)bytes)
		fatal_error("Error: Not enough memory or data\n");
}

static void
convert1555to32(int width, int height, unsigned char *inbuffer,
                unsigned char *outbuffer)
{
	unsigned int i;

	for (i = 0; i < (unsigned int)height * width * 2; i += 2) {
		/* BLUE  = 0 */
		outbuffer[(i << 1) + Blue] = (inbuffer[i + 1] & 0x7C) << 1;
		/* GREEN = 1 */
		outbuffer[(i << 1) + Green] = ((inbuffer[i + 1] & 0x3) << 3
		                             | (inbuffer[i] & 0xe0) >> 5) << 3;
		/* RED   = 2 */
		outbuffer[(i << 1) + Red] = (inbuffer[i] & 0x1f) << 3;
		/* ALPHA = 3 */
		outbuffer[(i << 1) + Alpha] = '\0';
	}
}

static void
convert565to32(int width, int height, unsigned char *inbuffer,
               unsigned char *outbuffer)
{
	unsigned int i;

	for (i = 0; i < (unsigned int)height * width * 2; i += 2) {
		/* BLUE  = 0 */
		outbuffer[(i << 1) + Blue] = (inbuffer[i] & 0x1f) << 3;
		/* GREEN = 1 */
		outbuffer[(i << 1) + Green] =
		        (((inbuffer[i + 1] & 0x7) << 3) | (inbuffer[i] & 0xE0)
		         >> 5) << 2;
		/* RED   = 2 */
		outbuffer[(i << 1) + Red] = inbuffer[i + 1] & 0xF8;
		/* ALPHA = 3 */
		outbuffer[(i << 1) + Alpha] = '\0';
	}
}

static void
convert888to32(int width, int height, unsigned char *inbuffer,
               unsigned char *outbuffer)
{
	unsigned int i;

	for (i = 0; i < (unsigned int)height * width; ++i) {
		/* BLUE  = 0 */
		outbuffer[(i << 2) + Blue] = inbuffer[i * 3 + srcBlue];
		/* GREEN = 1 */
		outbuffer[(i << 2) + Green] = inbuffer[i * 3 + srcGreen];
		/* RED   = 2 */
		outbuffer[(i << 2) + Red] = inbuffer[i * 3 + srcRed];
		/* ALPHA */
		outbuffer[(i << 2) + Alpha] = '\0';
	}
}

static void
convert8888to32(int width, int height, unsigned char *inbuffer,
                unsigned char *outbuffer)
{
	unsigned int i;

	for (i = 0; i < (unsigned int)height * width; ++i) {
		/* BLUE  = 0 */
		outbuffer[(i << 2) + Blue] = inbuffer[i * 4 + srcBlue];
		/* GREEN = 1 */
		outbuffer[(i << 2) + Green] = inbuffer[i * 4 + srcGreen];
		/* RED   = 2 */
		outbuffer[(i << 2) + Red] = inbuffer[i * 4 + srcRed];
		/* ALPHA */
		outbuffer[(i << 2) + Alpha] = srcAlpha >= 0
		                            ? inbuffer[i * 4 + srcAlpha]
		                            : 0;
	}
}

static void
write_PNG(unsigned char *outbuffer, char *filename, int width, int height,
          int interlace, int compression)
{
	int i;
	int bit_depth = 0, color_type;
	png_bytep row_pointers[height];
	png_structp png_ptr;
	png_infop info_ptr;
	FILE *outfile;

	if (strcmp(filename, "-") == 0)
		outfile = stdout;
	else {
		outfile = fopen(filename, "wb");
		if (!outfile) {
			fprintf(stderr, "Error: Couldn't fopen %s.\n",
			        filename);
			exit(EXIT_FAILURE);
		}
	}

	for (i = 0; i < height; i++)
		row_pointers[i] = outbuffer + i * 4 * width;

	png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING,
	                                  (png_voidp)NULL, (png_error_ptr)NULL,
	                                  (png_error_ptr)NULL);

	if (!png_ptr)
		fatal_error("Error: Couldn't create PNG write struct.");

	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		fatal_error("Error: Couldn't create PNG info struct.");
	}

	png_init_io(png_ptr, outfile);

	png_set_compression_level(png_ptr, Z_BEST_COMPRESSION);

	bit_depth = 8;
	color_type = PNG_COLOR_TYPE_RGB_ALPHA;
	png_set_invert_alpha(png_ptr);
	png_set_bgr(png_ptr);

	png_set_IHDR(png_ptr, info_ptr, width, height, bit_depth, color_type,
	             interlace, PNG_COMPRESSION_TYPE_DEFAULT,
	             PNG_FILTER_TYPE_DEFAULT);

	png_write_info(png_ptr, info_ptr);

	fprintf(stderr, "Now writing PNG file (compression %d)\n",
	        compression);

	png_write_image(png_ptr, row_pointers);

	png_write_end(png_ptr, info_ptr);
	/* puh, done, now freeing memory... */
	png_destroy_write_struct(&png_ptr, &info_ptr);

	if (outfile != NULL)
		fclose(outfile);
}

static void
convert_and_write(unsigned char *inbuffer, char *filename, int width,
                  int height, int bits, int interlace, int compression)
{
	size_t bufsize = (size_t)width * height * 4;

	unsigned char *outbuffer = malloc(bufsize);

	if (outbuffer == NULL)
		fatal_error("Not enough memory");

	memset(outbuffer, 0, bufsize);

	fprintf(stderr, "Converting image from %i\n", bits);

	switch (bits) {
	case 15:
		convert1555to32(width, height, inbuffer, outbuffer);
		write_PNG(outbuffer, filename, width, height, interlace,
		          compression);
		break;
	case 16:
		convert565to32(width, height, inbuffer, outbuffer);
		write_PNG(outbuffer, filename, width, height, interlace,
		          compression);
		break;
	case 24:
		convert888to32(width, height, inbuffer, outbuffer);
		write_PNG(outbuffer, filename, width, height, interlace,
		          compression);
		break;
	case 32:
		convert8888to32(width, height, inbuffer, outbuffer);
		write_PNG(outbuffer, filename, width, height, interlace,
		          compression);
		break;
	default:
		fprintf(stderr, "%d bits per pixel are not supported! ", bits);
		exit(EXIT_FAILURE);
	}

	free(outbuffer);
}

int
main(int argc, char **argv)
{
	unsigned char *buf_p;
	char *device = NULL;
	char *outfile = argv[argc - 1];
	int optc;
	int vt_num = UNDEFINED, bitdepth = UNDEFINED, height = UNDEFINED,
	    width = UNDEFINED;
	int old_vt = UNDEFINED;
	size_t buf_size;
	char infile[MAX_LEN];
	struct fb_var_screeninfo fb_varinfo;
	struct fb_fix_screeninfo fb_fixinfo;
	int waitbfg = 0; /* wait before grabbing (for -C ) */
	int interlace = PNG_INTERLACE_NONE;
	int verbose = 0;
	int png_compression = Z_DEFAULT_COMPRESSION;
	int skip_bytes = 0;

	memset(infile, 0, MAX_LEN);
	memset(&fb_varinfo, 0, sizeof fb_varinfo);
	memset(&fb_fixinfo, 0, sizeof fb_fixinfo);

	opterr = 0;
	while ((optc = getopt(argc, argv, "b:C:c:d:f:h:is:vw:z:")) != -1) {
		switch (optc) {
		case 'b':
			bitdepth = myatoi(optarg);
			break;
		case 'C':
			waitbfg = 1;
			/* fallthrough */
		case 'c':
			vt_num = myatoi(optarg);
			break;
		case 'd':
			device = optarg;
			break;
		case 'f':
			strncpy(infile, optarg, MAX_LEN);
			break;
		case 'h':
			height = myatoi(optarg);
			break;
		case 'i':
			interlace = PNG_INTERLACE_ADAM7;
			break;
		case 'v':
			verbose = 1;
			break;
		case 's':
			sleep((unsigned int)myatoi(optarg));
			break;
		case 'w':
			width = myatoi(optarg);
			break;
		case 'z':
			png_compression = myatoi(optarg);
			break;
		default:
			usage(argv[0]);
		}
	}

	if (argc - optind != 1) {
		usage(argv[0]);
		return 1;
	}

	if (UNDEFINED != vt_num) {
		old_vt = (int)change_to_vt((unsigned short int)vt_num);
		if (waitbfg != 0)
			sleep(3);
	}

	if (strlen(infile) > 0) {
		if (UNDEFINED == bitdepth || UNDEFINED == width
		 || UNDEFINED == height) {
			fprintf(stderr,
			        "Width, height and bitdepth are mandatory when reading from file\n");
			exit(EXIT_FAILURE);
		}
	} else {
		if (NULL == device) {
			device = getenv("FRAMEBUFFER");
			if (NULL == device)
				device = DEFAULT_FB;
		}

		get_framebufferdata(device, &fb_varinfo, &fb_fixinfo, verbose);

		if (UNDEFINED == bitdepth)
			bitdepth = (int)fb_varinfo.bits_per_pixel;

		if (UNDEFINED == width)
			width = (int)fb_fixinfo.line_length
		              / (fb_varinfo.bits_per_pixel / 8);

		if (UNDEFINED == height)
			height = (int)fb_varinfo.yres;

		skip_bytes =
		        (fb_varinfo.yoffset * fb_varinfo.xres) *
		        (fb_varinfo.bits_per_pixel >> 3);

		fprintf(stderr, "Resolution: %ix%i depth %i\n", width, height,
		        bitdepth);

		strncpy(infile, device, MAX_LEN - 1);
	}

	buf_size = width * height * (((unsigned int)bitdepth + 7) >> 3);

	buf_p = malloc(buf_size);

	if (buf_p == NULL)
		fatal_error("Not enough memory");

	memset(buf_p, 0, buf_size);

	read_framebuffer(infile, buf_size, buf_p, skip_bytes);

	if (UNDEFINED != old_vt)
		change_to_vt((unsigned short int)old_vt);

	convert_and_write(buf_p, outfile, width, height, bitdepth, interlace,
	                  png_compression);

	free(buf_p);
}
