// Checks every language row for complete, well-formed UTF-8 strings.
#include "Arduino.h"
#include "Preferences.h"
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
#include "pet.h"  // MED_COUNT
#include <cstdio>
#include <cstring>

static const char *LANG[] = { "ES", "EN", "FR", "DE", "IT", "PT", "ZH-TW" };

static bool validUtf8(const char *s) {
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    if (*p < 0x80) { ++p; continue; }
    unsigned need = (*p >= 0xC2 && *p <= 0xDF) ? 1 :
                    (*p >= 0xE0 && *p <= 0xEF) ? 2 :
                    (*p >= 0xF0 && *p <= 0xF4) ? 3 : 99;
    if (need == 99) return false;
    ++p;
    for (unsigned i = 0; i < need; ++i, ++p)
      if ((*p & 0xC0) != 0x80 || !*p) return false;
  }
  return true;
}

int main() {
  int bad = 0;
  for (int l = 0; l < LANG_COUNT; l++) {
    setLang((Lang)l);
    for (int s = 0; s < STR_COUNT; s++) {
      const char *v = T((StrId)s);
      if (!v || !validUtf8(v)) {
        printf("INVALID  %s index %d: %s\n", LANG[l], s, v ? v : "(null)");
        bad++;
      }
    }
    for (int m = 0; m < MED_COUNT; ++m) {
      if (!validUtf8(medalName(m)) || !validUtf8(medalLabel(m)) || !validUtf8(medalDesc(m))) {
        printf("INVALID MEDAL  %s index %d\n", LANG[l], m);
        bad++;
      }
    }
  }
  setLang(LANG_ZH_TW);
  if (strcmp(T(S_MENU_TITLE), "選單") != 0) {
    printf("Traditional Chinese language selection failed\n");
    bad++;
  }
  printf("%s: %d languages x %d strings, UTF-8 checked\n",
         bad ? "FAIL" : "PASS", LANG_COUNT, STR_COUNT);
  return bad ? 1 : 0;
}
