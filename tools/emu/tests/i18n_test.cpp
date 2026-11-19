// Checks the positional STRINGS table: a language row with too few entries is
// zero-padded by the compiler with no diagnostic, which silently shifts every
// string after the gap. Enforces ASCII-only for western languages, valid UTF-8
// + CJK bitmap glyph coverage for LANG_ZH_TW, printf format-specifier parity
// against LANG_EN, and completeness of all localized game-data accessors.
#include "Arduino.h"
#include "Preferences.h"
// linked against the same core as every other suite, so it needs the same
// hardware stubs even though it only exercises the string table
uint32_t g_seed = 1;
FakeSerial Serial; FakeESP ESP; FakeWire Wire;
volatile int g_touchX = 0, g_touchY = 0;
volatile bool g_touchDown = false;
bool wasPressed = false;
uint32_t millis() { return 0; }
void FakeESP::restart() { exit(0); }
int FakeSerial::available() { return 0; }
String FakeSerial::readStringUntil(char) { return String(""); }
void sfxPlay(uint8_t) {}
#include "i18n.h"
#include "cjk_text.h"
#include "pet.h"
#include "dex.h"
#include "moves.h"
#include "avatars.h"
#include <cstdio>
#include <cstring>
#include <string>

// STRINGS is file-static, so go through the same accessor the firmware uses.
static const char *LANG[] = { "ES", "EN", "FR", "DE", "IT", "PT", "ZH_TW" };

static bool checkUtf8AndGlyphs(const char *ctx, const char *s) {
  if (!s || !*s) {
    printf("EMPTY  %s\n", ctx);
    return false;
  }
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    if (*p < 0x80) {
      p++;
      continue;
    }
    uint32_t cp = 0;
    if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
      cp = ((uint32_t)(p[0] & 0x1F) << 6) | (p[1] & 0x3F);
      p += 2;
    } else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
      cp = ((uint32_t)(p[0] & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F);
      p += 3;
    } else {
      printf("BAD-UTF8  %s: \"%s\"\n", ctx, s);
      return false;
    }
    if (cp > 0xFFFF || !cjkHasGlyph((uint16_t)cp)) {
      printf("NO-GLYPH  %s U+%04X in \"%s\"\n", ctx, (unsigned)cp, s);
      return false;
    }
  }
  return true;
}

static std::string extractFmt(const char *s) {
  std::string out;
  for (const char *p = s; *p; p++) {
    if (*p != '%') continue;
    p++;
    if (*p == '%') continue;
    while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
    while (*p >= '0' && *p <= '9') p++;
    if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
    while (*p == 'l' || *p == 'h' || *p == 'z') { out.push_back(*p); p++; }
    if (*p) { out.push_back(*p); out.push_back('|'); }
  }
  return out;
}

int main() {
  int bad = 0;
  if ((int)(sizeof(LANG) / sizeof(LANG[0])) != LANG_COUNT) {
    printf("LANG array size mismatch\n");
    return 1;
  }

  for (int l = 0; l < LANG_COUNT; l++) {
    for (int s = 0; s < STR_COUNT; s++) {
      setLang((Lang)l);
      const char *v = T((StrId)s);
      if (!v) { printf("NULL  %s index %d\n", LANG[l], s); bad++; continue; }
      if (l == LANG_ZH_TW) {
        if (*v) {
          char ctx[48];
          snprintf(ctx, sizeof(ctx), "ZH_TW str[%d]", s);
          if (!checkUtf8AndGlyphs(ctx, v)) bad++;
        }
      } else {
        for (const unsigned char *p = (const unsigned char *)v; *p; p++)
          if (*p > 0x7F) { printf("NON-ASCII  %s index %d: \"%s\"\n", LANG[l], s, v); bad++; break; }
      }
      setLang(LANG_EN);
      const char *en = T((StrId)s);
      if (en && extractFmt(en) != extractFmt(v)) {
        printf("FMT-MISMATCH  %s index %d: en=\"%s\" vs \"%s\"\n", LANG[l], s, en, v);
        bad++;
      }
    }

    setLang((Lang)l);
    for (int m = 0; m < MED_COUNT; m++) {
      const char *ms[3] = { medalName(m), medalLabel(m), medalDesc(m) };
      for (int k = 0; k < 3; k++) {
        if (!ms[k] || !*ms[k]) {
          printf("EMPTY-MEDAL  %s medal[%d][%d]\n", LANG[l], m, k);
          bad++;
          continue;
        }
        if (l == LANG_ZH_TW) {
          char ctx[48];
          snprintf(ctx, sizeof(ctx), "ZH_TW medal[%d][%d]", m, k);
          if (!checkUtf8AndGlyphs(ctx, ms[k])) bad++;
        } else {
          for (const unsigned char *p = (const unsigned char *)ms[k]; *p; p++)
            if (*p > 0x7F) { printf("NON-ASCII-MEDAL  %s medal[%d][%d]: \"%s\"\n", LANG[l], m, k, ms[k]); bad++; break; }
        }
      }
    }
  }

  // Verify every Traditional Chinese localized data table entry has valid UTF-8 and glyphs
  setLang(LANG_ZH_TW);
  if (!checkUtf8AndGlyphs("lang-code", "\xE7\xB9\x81\xE4\xB8\xAD")) bad++;
  for (int d = 1; d <= DEX_COUNT; d++) {
    char ctx[40]; snprintf(ctx, sizeof(ctx), "species[%d]", d);
    if (!checkUtf8AndGlyphs(ctx, speciesName((int16_t)d))) bad++;
  }
  for (int m = 0; m < MOVE_COUNT; m++) {
    char ctx[40]; snprintf(ctx, sizeof(ctx), "move[%d]", m);
    if (!checkUtf8AndGlyphs(ctx, moveName((uint8_t)m))) bad++;
  }
  for (int t = 0; t < TYPE_COUNT; t++) {
    char ctx[40]; snprintf(ctx, sizeof(ctx), "type[%d]", t);
    if (!checkUtf8AndGlyphs(ctx, typeNameLoc((uint8_t)t))) bad++;
  }
  for (int r = 0; r < REGION_COUNT; r++) {
    char ctx[40]; snprintf(ctx, sizeof(ctx), "region[%d]", r);
    if (!checkUtf8AndGlyphs(ctx, regionNameLoc((uint8_t)r))) bad++;
  }
  for (int r = 0; r < GYM_REGIONS; r++) {
    for (int i = 0; i < TRAINER_COUNT; i++) {
      char ctx[48];
      snprintf(ctx, sizeof(ctx), "trainer[%d][%d].name", r, i);
      if (!checkUtf8AndGlyphs(ctx, trainerNameLoc((uint8_t)r, (uint8_t)i))) bad++;
      snprintf(ctx, sizeof(ctx), "trainer[%d][%d].place", r, i);
      if (!checkUtf8AndGlyphs(ctx, trainerPlaceLoc((uint8_t)r, (uint8_t)i))) bad++;
    }
  }
  for (int a = 0; a < AVATAR_COUNT; a++) {
    char ctx[40]; snprintf(ctx, sizeof(ctx), "avatar[%d]", a);
    if (!checkUtf8AndGlyphs(ctx, avatarNameLoc((uint8_t)a))) bad++;
  }

  printf("%s: %d languages x %d strings (+ zh-TW data tables)\n",
         bad ? "FAIL" : "PASS", LANG_COUNT, STR_COUNT);
  return bad ? 1 : 0;
}
