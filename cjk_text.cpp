#include "cjk_text.h"
#include "cjk_font.h"

bool uiHasUtf8(const char *s) {
  if (!s) return false;
  for (const uint8_t *p = (const uint8_t *)s; *p; p++) {
    if (*p >= 0x80) return true;
  }
  return false;
}

uint16_t uiNextUtf8(const char **p) {
  const uint8_t *s = (const uint8_t *)(*p);
  uint8_t c0 = s[0];
  if (c0 < 0x80) {
    *p += 1;
    return c0;
  }
  if ((c0 & 0xE0) == 0xC0 && s[1]) {
    uint16_t cp = (uint16_t)(((c0 & 0x1F) << 6) | (s[1] & 0x3F));
    *p += 2;
    return cp;
  }
  if ((c0 & 0xF0) == 0xE0 && s[1] && s[2]) {
    uint16_t cp = (uint16_t)(((c0 & 0x0F) << 12) |
                             ((s[1] & 0x3F) << 6) |
                             (s[2] & 0x3F));
    *p += 3;
    return cp;
  }
  // Skip 4-byte sequences or malformed leading bytes safely.
  if ((c0 & 0xF8) == 0xF0 && s[1] && s[2] && s[3]) {
    *p += 4;
    return 0xFFFD;
  }
  *p += 1;
  return 0xFFFD;
}

int cjkFindGlyph(uint16_t cp) {
  int lo = 0, hi = CJK_COUNT - 1;
  while (lo <= hi) {
    int mid = lo + ((hi - lo) >> 1);
    uint16_t v = CJK_CP[mid];
    if (v == cp) return mid;
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return -1;
}

bool cjkHasGlyph(uint16_t cp) {
  return cjkFindGlyph(cp) >= 0;
}

const uint8_t *cjkGlyph12(int idx) {
  return (idx >= 0 && idx < CJK_COUNT) ? CJK_12[idx] : nullptr;
}

const uint8_t *cjkGlyph16(int idx) {
  return (idx >= 0 && idx < CJK_COUNT) ? CJK_16[idx] : nullptr;
}

int uiCharAdvance(uint16_t cp, uint8_t size) {
  if (!size) size = 1;
  if (cp < 0x80) return 6 * size;
  if (size == 1) return 13;
  if (size == 2) return 17;
  if (size == 3) return 26;
  int sc = size / 2;
  if (sc < 1) sc = 1;
  return 17 * sc;
}

int uiTextWidth(const char *s, uint8_t size) {
  if (!s) return 0;
  if (!size) size = 1;
  int w = 0;
  const char *p = s;
  while (*p) {
    uint16_t cp = uiNextUtf8(&p);
    w += uiCharAdvance(cp, size);
  }
  return w;
}
