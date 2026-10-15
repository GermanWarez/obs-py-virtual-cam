/*
 * test_text_render.c — plain-clang unit test for the BGRA text renderer.
 *
 * Build & run (no libobs):
 *   clang -std=c17 -o /tmp/ttr tests/test_text_render.c src/text_render.c && /tmp/ttr
 *
 * Verifies that:
 *   - a known glyph lights the pixels its bitmap says it should, and leaves
 *     background pixels unlit;
 *   - the background is the expected dark colour with opaque alpha;
 *   - toggling the cursor flag changes pixels at the cursor position.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/font8x16.h"
#include "../src/text_render.h"

/* Must match text_render.c. */
#define MARGIN_X 4
#define MARGIN_Y 4
#define BG_B 0x18
#define BG_G 0x14
#define BG_R 0x10
#define FG_B 0xC8
#define FG_G 0xD0
#define FG_R 0xC8

#define WIDTH 64
#define HEIGHT 48

static int failures = 0;

#define CHECK(cond, msg)                                      \
	do {                                                  \
		if (!(cond)) {                                \
			printf("FAIL: %s\n", (msg));          \
			failures++;                           \
		}                                             \
	} while (0)

static const uint8_t *pixel_at(const uint8_t *buf, int x, int y)
{
	return buf + ((size_t)y * WIDTH + (size_t)x) * 4u;
}

static bool pixel_is_fg(const uint8_t *buf, int x, int y)
{
	const uint8_t *p = pixel_at(buf, x, y);
	return p[0] == FG_B && p[1] == FG_G && p[2] == FG_R && p[3] == 0xFF;
}

static bool pixel_is_bg(const uint8_t *buf, int x, int y)
{
	const uint8_t *p = pixel_at(buf, x, y);
	return p[0] == BG_B && p[1] == BG_G && p[2] == BG_R && p[3] == 0xFF;
}

int main(void)
{
	uint8_t *buf = malloc((size_t)WIDTH * HEIGHT * 4u);
	if (!buf) {
		printf("FAIL: allocation\n");
		return 1;
	}

	/* Render a single 'A' (0x41) at the top-left, cursor off. */
	text_render_bgra(buf, WIDTH, HEIGHT, "A", false);

	/* Background sanity: a pixel far from the glyph must be the dark bg. */
	CHECK(pixel_is_bg(buf, WIDTH - 1, HEIGHT - 1), "corner pixel is background");

	/* Compare every pixel of the glyph cell against the font bitmap. */
	const uint8_t *glyph = FONT8X16['A' - FONT8X16_FIRST_CHAR];
	int lit_fg = 0, lit_bg = 0;
	for (int row = 0; row < FONT8X16_HEIGHT; row++) {
		for (int col = 0; col < FONT8X16_WIDTH; col++) {
			int x = MARGIN_X + col;
			int y = MARGIN_Y + row;
			bool should_light = (glyph[row] & (1u << col)) != 0;
			if (should_light) {
				if (pixel_is_fg(buf, x, y)) {
					lit_fg++;
				}
			} else {
				if (pixel_is_bg(buf, x, y)) {
					lit_bg++;
				}
			}
		}
	}
	/* 'A' has lit pixels; make sure we actually matched some and that unlit
	 * cells stayed background. */
	CHECK(lit_fg > 0, "glyph 'A' lit at least one foreground pixel");
	CHECK(lit_bg > 0, "glyph 'A' left unlit cells as background");

	/* Exact bitmap match: every cell must agree with the font. */
	bool exact = true;
	for (int row = 0; row < FONT8X16_HEIGHT && exact; row++) {
		for (int col = 0; col < FONT8X16_WIDTH; col++) {
			int x = MARGIN_X + col;
			int y = MARGIN_Y + row;
			bool should_light = (glyph[row] & (1u << col)) != 0;
			if (should_light && !pixel_is_fg(buf, x, y)) {
				exact = false;
				break;
			}
			if (!should_light && !pixel_is_bg(buf, x, y)) {
				exact = false;
				break;
			}
		}
	}
	CHECK(exact, "glyph 'A' matches font bitmap exactly");

	/* Cursor toggling: with one character drawn, the cursor sits in the next
	 * cell (column 1). That cell is background with the cursor off. */
	int cursor_x = MARGIN_X + 1 * FONT8X16_WIDTH + 2; /* interior of the cell */
	int cursor_y = MARGIN_Y + 4;
	CHECK(pixel_is_bg(buf, cursor_x, cursor_y), "cursor cell is background when cursor off");

	/* Now render with the cursor on; that same cell must become foreground. */
	text_render_bgra(buf, WIDTH, HEIGHT, "A", true);
	CHECK(pixel_is_fg(buf, cursor_x, cursor_y), "cursor cell is foreground when cursor on");

	/* The drawn glyph should be unchanged by the cursor flag. */
	bool glyph_still_there = false;
	for (int col = 0; col < FONT8X16_WIDTH; col++) {
		for (int row = 0; row < FONT8X16_HEIGHT; row++) {
			if ((glyph[row] & (1u << col)) && pixel_is_fg(buf, MARGIN_X + col, MARGIN_Y + row)) {
				glyph_still_there = true;
			}
		}
	}
	CHECK(glyph_still_there, "glyph remains drawn with cursor on");

	free(buf);

	if (failures == 0) {
		printf("ok: all text_render assertions passed\n");
		return 0;
	}
	printf("%d assertion(s) failed\n", failures);
	return 1;
}
