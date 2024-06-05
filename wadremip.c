/*
 * wadremip - generate better mips for quake WADs
 * by erysdren
 *
 * license: public domain
 */

/*
 *
 * https://medium.com/@chathuragunasekera/image-resampling-algorithms-for-pixel-manipulation-bee65dda1488
 * https://www.scratchapixel.com/lessons/mathematics-physics-for-computer-graphics/interpolation/bilinear-filtering.html
 * https://gist.github.com/folkertdev/6b930c7a7856e36dcad0a72a03e66716
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>

#include "quake_palette.h"

static const uint32_t bsp_magic = 0x0000001D; /* 29 */
static const uint32_t wad2_magic = 0x32444157; /* WAD2 */
enum {
	WAD2_LUMP_PALETTE = 0x40,
	WAD2_LUMP_SBAR = 0x42,
	WAD2_LUMP_MIPTEX = 0x44,
	WAD2_LUMP_CONPIC = 0x45
};

typedef struct mipheader {
	int32_t num_textures;
	int32_t *ofs_textures;
} mipheader_t;

typedef struct miptex {
	char name[16];
	int32_t width;
	int32_t height;
	int32_t offsets[4];
} miptex_t;

typedef struct wad {
	int32_t num_lumps;
	struct wad_lump {
		int32_t ofs_data;
		int32_t len_data;
		int32_t len_data_uncompressed;
		uint8_t type;
		uint8_t compression;
		uint16_t _padding;
		char name[16];
	} *lumps;
} wad_t;

static wad_t *wad = NULL;
static FILE *file = NULL;
static miptex_t *miptex = NULL;

wad_t *wad_allocate(int32_t num_lumps)
{
	wad_t *wad;

	wad = calloc(1, sizeof(wad_t));
	wad->lumps = calloc(num_lumps, sizeof(struct wad_lump));
	wad->num_lumps = num_lumps;

	return wad;
}

void wad_free(wad_t *wad)
{
	free(wad->lumps);
	free(wad);
}

void reset(const char *s, ...)
{
	static char buffer[1024];
	va_list ap;

	va_start(ap, s);
	vsnprintf(buffer, sizeof(buffer), s, ap);
	va_end(ap);

	fprintf(stderr, "warning: %s\n", buffer);

	if (wad)
		wad_free(wad);

	if (file)
		fclose(file);

	if (miptex)
		free(miptex);

	wad = NULL;
	file = NULL;
	miptex = NULL;
}

static inline int isqr(int a) { return a * a; }
static inline int imin(int x, int y) { return x < y ? x : y; }
static inline int imax(int x, int y) { return x > y ? x : y; }

static int palette_search(uint8_t palette[768], int r, int g, int b)
{
	int i, pen, dist = INT32_MAX;

	for (i = 256; i--;)
	{
		int rank = isqr(palette[i * 3] - r) +
			isqr(palette[(i * 3) + 1] - g) +
			isqr(palette[(i * 3) + 2] - b);

		if (rank < dist)
		{
			pen = i;
			dist = rank;
		}
	}

	return pen;
}

static int blend4_u8(float alpha, float beta, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint8_t *palette)
{
	uint8_t *a_rgb, *b_rgb, *c_rgb, *d_rgb;
	int i;
	int result[3];

	a_rgb = &palette[a * 3];
	b_rgb = &palette[b * 3];
	c_rgb = &palette[c * 3];
	d_rgb = &palette[d * 3];

	for (i = 0; i < 3; i++)
	{
		result[i] = (1.0 - alpha) * (1.0 - beta) * a_rgb[i] +
			alpha * (1.0 - beta) * b_rgb[i] +
			(1.0 - alpha) * beta * c_rgb[i] +
			alpha * beta * d_rgb[i];
	}

	return palette_search(palette, result[0], result[1], result[2]);
}

static uint8_t *bilinear_u8(uint8_t *image, int w, int h, int new_w, int new_h, uint8_t *palette)
{
	uint8_t *out;
	int x, y;
	float xscale, yscale;
	int xhigh, yhigh, xlow, ylow;
	float src_x, src_y;
	uint8_t a, b, c, d;
	float alpha, beta;

	/* allocate return image */
	out = calloc(1, new_w * new_h);

	/* get x and y scale */
	xscale = (float)w / (float)new_w;
	yscale = (float)h / (float)new_h;

	/* loop over all pixels in the new image */
	for (y = 0; y < new_h; y++)
	{
		for (x = 0; x < new_w; x++)
		{
			/* get weights */
			src_x = (float)x * xscale;
			src_y = (float)y * yscale;

			/* get high and low pixels */
			xlow = (int)src_x;
			ylow = (int)src_y;
			xhigh = imin(xlow + 1, w - 1);
			yhigh = imin(ylow + 1, h - 1);

			/* get high and low pixel values */
			a = image[ylow * w + xlow];
			b = image[ylow * w + xhigh];
			c = image[yhigh * w + xlow];
			d = image[yhigh * w + xhigh];

			/* if any of these are a transparent index, just write that */
			if (a == 255 || b == 255 || c == 255 || d == 255)
			{
				out[y * new_w + x] = 255;
			}
			else
			{
				alpha = src_x - (float)xlow;
				beta = src_y - (float)ylow;
				out[y * new_w + x] = blend4_u8(alpha, beta, a, b, c, d, palette);
			}
		}
	}

	return out;
}

void process_bsp(void)
{
	int32_t ofs_miptex, len_miptex, num_textures;
	int32_t *ofs_textures;
	int t, m;

	/* read lump offset */
	fseek(file, 20, SEEK_SET);
	fread(&ofs_miptex, 4, 1, file);
	fread(&len_miptex, 4, 1, file);

	/* read number of textures */
	fseek(file, ofs_miptex, SEEK_SET);
	fread(&num_textures, 4, 1, file);

	/* read texture offsets */
	ofs_textures = calloc(4, num_textures);
	fread(ofs_textures, 4, num_textures, file);

	miptex = calloc(1, sizeof(miptex_t));

	/* loop over all miptextures */
	for (t = 0; t < num_textures; t++)
	{
		uint8_t *src;

		/* read miptex */
		fseek(file, ofs_miptex + ofs_textures[t], SEEK_SET);
		fread(miptex, sizeof(miptex_t), 1, file);

		/* read mip 0 */
		src = calloc(1, miptex->width * miptex->height);
		fseek(file, ofs_miptex + ofs_textures[t] + miptex->offsets[0], SEEK_SET);
		fread(src, miptex->width * miptex->height, 1, file);

		/* generate 3 mip levels */
		for (m = 1; m < 4; m++)
		{
			int new_w, new_h;
			uint8_t *mip;

			new_w = miptex->width >> m;
			new_h = miptex->height >> m;

			/* generate mip */
			mip = bilinear_u8(src, miptex->width, miptex->height, new_w, new_h, quake_palette);

			/* write it */
			fseek(file, ofs_miptex + ofs_textures[t] + miptex->offsets[m], SEEK_SET);
			fwrite(mip, new_w * new_h, 1, file);

			free(mip);
		}

		printf("Generated new mips for \"%s\"\n", miptex->name);

		free(src);
	}

	/* cleanup */
	free(ofs_textures);
	free(miptex);
	fclose(file);
	file = NULL;
	miptex = NULL;
}

void process_wad(void)
{
	int32_t num_lumps;
	int32_t ofs_lumps;
	int l, m;

	/* read wad header */
	fread(&num_lumps, 4, 1, file);
	fread(&ofs_lumps, 4, 1, file);

	/* allocate wad structure */
	wad = wad_allocate(num_lumps);

	/* read lumps */
	fseek(file, ofs_lumps, SEEK_SET);
	fread(wad->lumps, sizeof(struct wad_lump), num_lumps, file);

	/* allocate a miptex structure */
	miptex = calloc(1, sizeof(miptex_t));

	/* go over all the lumps */
	for (l = 0; l < num_lumps; l++)
	{
		uint8_t *src;

		if (wad->lumps[l].type != WAD2_LUMP_MIPTEX)
		{
			printf("\"%s\" is not a miptex lump\n", wad->lumps[l].name);
			continue;
		}

		/* read miptex data */
		fseek(file, wad->lumps[l].ofs_data, SEEK_SET);
		fread(miptex, sizeof(miptex_t), 1, file);

		/* read mip 0 */
		src = calloc(1, miptex->width * miptex->height);
		fseek(file, wad->lumps[l].ofs_data + miptex->offsets[0], SEEK_SET);
		fread(src, miptex->width * miptex->height, 1, file);

		/* generate 3 mip levels */
		for (m = 1; m < 4; m++)
		{
			int new_w, new_h;
			uint8_t *mip;

			new_w = miptex->width >> m;
			new_h = miptex->height >> m;

			/* generate mip */
			mip = bilinear_u8(src, miptex->width, miptex->height, new_w, new_h, quake_palette);

			/* write it */
			fseek(file, wad->lumps[l].ofs_data + miptex->offsets[m], SEEK_SET);
			fwrite(mip, new_w * new_h, 1, file);

			free(mip);
		}

		printf("Generated new mips for \"%s\"\n", wad->lumps[l].name);

		free(src);
	}

	wad_free(wad);
	fclose(file);
	free(miptex);

	wad = NULL;
	file = NULL;
	miptex = NULL;
}

int main(int argc, char **argv)
{
	uint32_t magic;
	int i;

	printf("Usage: wadremip <tex1.wad> <tex2.wad> <map1.bsp> <map2.bsp> ...\n");

	for (i = 1; i < argc; i++)
	{
		file = fopen(argv[i], "rb+");
		if (file == NULL)
		{
			reset("Could not open \"%s\" for editing.", argv[i]);
			continue;
		}

		/* check wad magic */
		fread(&magic, 4, 1, file);
		if (memcmp(&magic, &wad2_magic, 4) == 0)
		{
			printf("Processing \"%s\" as WAD\n", argv[i]);
			process_wad();
		}
		else if (memcmp(&magic, &bsp_magic, 4) == 0)
		{
			printf("Processing \"%s\" as BSP\n", argv[i]);
			process_bsp();
		}
		else
		{
			reset("File signature 0x%04x does not match the signature for either WAD or BSP.", magic);
			continue;
		}

		printf("Finished processing \"%s\"\n", argv[i]);
	}

	return EXIT_SUCCESS;
}
