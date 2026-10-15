/*
 * text_render.h — BGRA placeholder text renderer.
 *
 * Draws a dark background and a block of monospaced text using the embedded
 * font8x16 glyph table, with an optional blinking block cursor at the end of
 * the text. Pure C with no libobs dependency so it is unit-testable with plain
 * clang (see tests/test_text_render.c).
 */

#ifndef PY_VIRTUAL_CAM_TEXT_RENDER_H
#define PY_VIRTUAL_CAM_TEXT_RENDER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Render multi-line monospaced text into a BGRA buffer.
 *
 *   buffer      destination, width*height*4 bytes, byte order B,G,R,A.
 *   width,height  buffer geometry in pixels.
 *   text        NUL-terminated string. '\n' starts a new line; '\t' expands to
 *               the next multiple of 4 columns. Printable ASCII 0x20..0x7E is
 *               drawn; other bytes render as a space. Text past the right edge
 *               is clipped per glyph; lines past the bottom edge are dropped.
 *   cursor_visible  when true, a filled block cursor is drawn at the position
 *               immediately after the last rendered character (used to animate
 *               a ~500ms blink by toggling this flag between frames).
 *
 * The whole buffer is first cleared to a dark background. Pixel alpha is set
 * opaque (0xFF). Safe for any width/height >= 0; a NULL buffer is a no-op.
 */
void text_render_bgra(uint8_t *buffer, int width, int height, const char *text, bool cursor_visible);

#ifdef __cplusplus
}
#endif

#endif /* PY_VIRTUAL_CAM_TEXT_RENDER_H */
