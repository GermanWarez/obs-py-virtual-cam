/*
 * text_render.c — BGRA placeholder text renderer (see text_render.h).
 *
 * No libobs dependency: only font8x16.h and the C standard library, so this
 * module compiles and runs under plain clang for unit testing.
 */

#include "text_render.h"

#include <stddef.h>

#include "font8x16.h"

/* Dark background (B, G, R) and foreground text colour. */
#define BG_B 0x18
#define BG_G 0x14
#define BG_R 0x10
#define FG_B 0xC8
#define FG_G 0xD0
#define FG_R 0xC8

/* Layout margins and tab width (in character columns). */
#define MARGIN_X 4
#define MARGIN_Y 4
#define TAB_COLUMNS 4

static void put_pixel(uint8_t *buffer, int width, int height, int x, int y, uint8_t b, uint8_t g,
		      uint8_t r)
{
	if (x < 0 || y < 0 || x >= width || y >= height) {
		return;
	}
	uint8_t *px = buffer + ((size_t)y * (size_t)width + (size_t)x) * 4u;
	px[0] = b;
	px[1] = g;
	px[2] = r;
	px[3] = 0xFF;
}

static void fill_background(uint8_t *buffer, int width, int height)
{
	size_t count = (size_t)width * (size_t)height;
	for (size_t i = 0; i < count; i++) {
		uint8_t *px = buffer + i * 4u;
		px[0] = BG_B;
		px[1] = BG_G;
		px[2] = BG_R;
		px[3] = 0xFF;
	}
}

/* Draw one glyph with its top-left corner at pixel (ox, oy). */
static void draw_glyph(uint8_t *buffer, int width, int height, int ox, int oy, unsigned char ch)
{
	if (ch < FONT8X16_FIRST_CHAR || ch > FONT8X16_LAST_CHAR) {
		ch = ' ';
	}
	const uint8_t *glyph = FONT8X16[ch - FONT8X16_FIRST_CHAR];
	for (int row = 0; row < FONT8X16_HEIGHT; row++) {
		uint8_t bits = glyph[row];
		for (int col = 0; col < FONT8X16_WIDTH; col++) {
			if (bits & (1u << col)) {
				put_pixel(buffer, width, height, ox + col, oy + row, FG_B, FG_G, FG_R);
			}
		}
	}
}

/* Draw a filled block cursor covering one glyph cell at (ox, oy). */
static void draw_cursor(uint8_t *buffer, int width, int height, int ox, int oy)
{
	for (int row = 0; row < FONT8X16_HEIGHT; row++) {
		for (int col = 0; col < FONT8X16_WIDTH; col++) {
			put_pixel(buffer, width, height, ox + col, oy + row, FG_B, FG_G, FG_R);
		}
	}
}

void text_render_bgra(uint8_t *buffer, int width, int height, const char *text, bool cursor_visible)
{
	if (buffer == NULL || width <= 0 || height <= 0) {
		return;
	}

	fill_background(buffer, width, height);

	/* Position of the cell following the last drawn character, for the cursor. */
	int cur_col = 0;
	int cur_line = 0;

	if (text != NULL) {
		int col = 0;  /* character column on the current line */
		int line = 0; /* zero-based line index */

		for (const char *p = text; *p != '\0'; p++) {
			unsigned char ch = (unsigned char)*p;

			if (ch == '\n') {
				line++;
				col = 0;
				cur_col = 0;
				cur_line = line;
				continue;
			}
			if (ch == '\r') {
				continue;
			}
			if (ch == '\t') {
				col = ((col / TAB_COLUMNS) + 1) * TAB_COLUMNS;
				cur_col = col;
				cur_line = line;
				continue;
			}

			int ox = MARGIN_X + col * FONT8X16_WIDTH;
			int oy = MARGIN_Y + line * FONT8X16_HEIGHT;

			/* Clip whole lines that fall below the buffer. */
			if (oy >= height) {
				break;
			}
			/* Clip glyphs that run past the right edge (per-glyph clip in
			 * put_pixel also guards partial glyphs). */
			if (ox < width) {
				draw_glyph(buffer, width, height, ox, oy, ch);
			}

			col++;
			cur_col = col;
			cur_line = line;
		}
	}

	if (cursor_visible) {
		int ox = MARGIN_X + cur_col * FONT8X16_WIDTH;
		int oy = MARGIN_Y + cur_line * FONT8X16_HEIGHT;
		if (ox < width && oy < height) {
			draw_cursor(buffer, width, height, ox, oy);
		}
	}
}
