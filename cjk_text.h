#pragma once
#include <stdint.h>
#include <stddef.h>

// UTF-8 decoding, width measurement, and 1-bit CJK bitmap text rendering.
// Pure C++ with no Arduino_GFX header dependency here; uiDrawText() is a
// template so it works identically with the real ESP32 Arduino_Canvas and
// the desktop emulator stand-in.

bool uiHasUtf8(const char *s);
uint16_t uiNextUtf8(const char **p);

int cjkFindGlyph(uint16_t cp);
bool cjkHasGlyph(uint16_t cp);
const uint8_t *cjkGlyph12(int idx);
const uint8_t *cjkGlyph16(int idx);

int uiCharAdvance(uint16_t cp, uint8_t size);
int uiTextWidth(const char *s, uint8_t size);

template <typename Gfx>
static inline void uiDrawBitmapRun(Gfx *g, int16_t x, int16_t y,
                                   const uint8_t *bm, int dim, int scale,
                                   uint16_t color) {
  int bitIdx = 0;
  for (int row = 0; row < dim; row++) {
    int col = 0;
    while (col < dim) {
      int bi = bitIdx + col;
      bool on = (bm[bi >> 3] & (0x80 >> (bi & 7))) != 0;
      if (!on) { col++; continue; }
      int start = col;
      while (col < dim) {
        int b2 = bitIdx + col;
        if (!(bm[b2 >> 3] & (0x80 >> (b2 & 7)))) break;
        col++;
      }
      g->fillRect(x + start * scale, y + row * scale,
                  (col - start) * scale, scale, color);
    }
    bitIdx += dim;
  }
}

template <typename Gfx>
static inline void uiDrawText(Gfx *g, int16_t x, int16_t y, const char *s,
                              uint8_t size, uint16_t color) {
  if (!s || !*s) return;
  if (!size) size = 1;
  g->setTextColor(color);
  g->setTextSize(size);
  if (!uiHasUtf8(s)) {
    g->setCursor(x, y);
    g->print(s);
    return;
  }

  int16_t cx = x;
  const char *p = s;
  while (*p) {
    if ((uint8_t)*p < 0x80) {
      // Batch contiguous ASCII characters so the built-in 5x7 renderer draws
      // them in one pass.
      char run[64];
      size_t n = 0;
      while (*p && (uint8_t)*p < 0x80 && n < sizeof(run) - 1) {
        if (*p == '\n') {
          if (n) {
            run[n] = 0;
            g->setCursor(cx, y);
            g->print(run);
            n = 0;
          }
          p++;
          cx = x;
          y += (size == 1) ? 13 : (8 * size);
          continue;
        }
        run[n++] = *p++;
      }
      if (n) {
        run[n] = 0;
        g->setCursor(cx, y);
        g->print(run);
        cx += (int16_t)(n * 6 * size);
      }
    } else {
      uint16_t cp = uiNextUtf8(&p);
      int idx = cjkFindGlyph(cp);
      int adv = uiCharAdvance(cp, size);
      if (idx >= 0) {
        if (size == 1) {
          uiDrawBitmapRun(g, cx, y - 2, cjkGlyph12(idx), 12, 1, color);
        } else if (size == 2) {
          uiDrawBitmapRun(g, cx, y, cjkGlyph16(idx), 16, 1, color);
        } else if (size == 3) {
          uiDrawBitmapRun(g, cx, y, cjkGlyph12(idx), 12, 2, color);
        } else {
          int sc = size / 2;
          if (sc < 1) sc = 1;
          uiDrawBitmapRun(g, cx, y, cjkGlyph16(idx), 16, sc, color);
        }
      } else {
        int h = (size == 1) ? 12 : (8 * size);
        int w = adv > 2 ? adv - 2 : adv;
        g->drawRect(cx, (size == 1) ? (y - 2) : y, w, h, color);
      }
      cx += (int16_t)adv;
    }
  }
}
