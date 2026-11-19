#include "sdmon.h"
#include "noart.h"
#include "pin_config.h"
#include "pet.h"   // gRegionArt, REGIONS -- the mask this narrows
#include <FS.h>
#include <LittleFS.h>
#include <miniz.h>

bool sdReady = false;
bool sdDirty = false;
bool sdArtDirty = false;
bool sdThumbsDirty = false;
SdThumbs thumbs;

// ---------------------------------------------------------------------------
// Compressed sprite loader (TPKZ container) backed by ESP32-S3 ROM miniz.
//
// Format of TPKZ (little-endian):
//   char[4]  "TPKZ"
//   uint32   rawSize  (uncompressed byte length)
//   uint8[]  zlib stream (RFC 1950, zlib.compress(raw, level))
//
// tinfl_decompressor is ~11 KB, which would overflow Arduino's 8 KB loopTask
// stack if placed on the stack (as tinfl_decompress_mem_to_mem does). Keeping
// a single reusable instance in PSRAM uses ~0 stack and <12 KB of PSRAM.
// ---------------------------------------------------------------------------
static tinfl_decompressor *gInfl = nullptr;

static bool inflateZlib(const uint8_t *src, size_t srcLen, uint8_t *dst, size_t dstLen) {
  if (!gInfl) gInfl = (tinfl_decompressor *)ps_malloc(sizeof(tinfl_decompressor));
  if (!gInfl) return false;
  tinfl_init(gInfl);
  size_t inBytes = srcLen;
  size_t outBytes = dstLen;
  tinfl_status st = tinfl_decompress(
      gInfl, src, &inBytes, dst, dst, &outBytes,
      TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
  return st == TINFL_STATUS_DONE && outBytes == dstLen;
}

// Reads a sprite file from LittleFS into a ps_malloc'd buffer. Transparently
// inflates TPKZ-compressed blobs while still accepting raw uncompressed files.
static uint8_t *readSpriteFile(const char *path, uint32_t *outSize, uint32_t maxSize) {
  File f = LittleFS.open(path, FILE_READ);
  if (!f) return nullptr;
  uint32_t fsize = f.size();
  if (fsize < 4 || fsize > maxSize) {
    f.close();
    return nullptr;
  }

  uint8_t magic[4];
  if (f.read(magic, 4) != 4) {
    f.close();
    return nullptr;
  }

  if (memcmp(magic, "TPKZ", 4) == 0) {
    uint32_t rawSize = 0;
    if (fsize <= 8 || f.read((uint8_t *)&rawSize, 4) != 4 ||
        rawSize < 4 || rawSize > maxSize) {
      f.close();
      return nullptr;
    }
    uint32_t compSize = fsize - 8;
    uint8_t *comp = (uint8_t *)ps_malloc(compSize);
    if (!comp || f.read(comp, compSize) != compSize) {
      if (comp) free(comp);
      f.close();
      return nullptr;
    }
    f.close();

    uint8_t *raw = (uint8_t *)ps_malloc(rawSize);
    if (!raw) {
      free(comp);
      return nullptr;
    }
    bool ok = inflateZlib(comp, compSize, raw, rawSize);
    free(comp);
    if (!ok) {
      Serial.printf("TPKZ inflate failed: %s\n", path);
      free(raw);
      return nullptr;
    }
    *outSize = rawSize;
    return raw;
  }

  // Uncompressed legacy file (TPK2 / TPTH / TPK1)
  uint8_t *raw = (uint8_t *)ps_malloc(fsize);
  if (!raw) {
    f.close();
    return nullptr;
  }
  memcpy(raw, magic, 4);
  if (fsize > 4 && f.read(raw + 4, fsize - 4) != fsize - 4) {
    free(raw);
    f.close();
    return nullptr;
  }
  f.close();
  *outSize = fsize;
  return raw;
}

bool PmdMon::load(int16_t dexNum, bool shiny) {
  // int16_t, NOT uint8_t. The dex reached 386 and this did not follow, so
  // everything from 256 up wrapped into Kanto: MARSHTOMP (258) opened
  // p002.bin and drew an IVYSAUR. Same trap that caught DexEntry::evolvesTo
  // and TrainerMon::dex when the expansion landed.
  if (dexNum < 1 || dexNum > 999) return false;
  unload();
  if (!sdReady) return false;

  char path[28];
  uint32_t size = 0;
  snprintf(path, sizeof(path), "/mons/p%s%03u.bin", shiny ? "s" : "", (unsigned)dexNum);
  blob = readSpriteFile(path, &size, 3UL * 1024 * 1024);
  if (!blob && shiny) {  // sin shiny PMD: usa el normal
    snprintf(path, sizeof(path), "/mons/p%03u.bin", (unsigned)dexNum);
    blob = readSpriteFile(path, &size, 3UL * 1024 * 1024);
  }
  if (!blob) return false;
  if (size < 7 || memcmp(blob, "TPK2", 4) != 0) {
    unload();
    return false;
  }

  dex = dexNum;                  // what is actually in here, for the tests
  uint8_t nActs = blob[4];
  memcpy(&palCount, blob + 5, 2);
  if (palCount > 256 || (uint32_t)7 + palCount * 2 > size) { unload(); return false; }
  memcpy(pal, blob + 7, palCount * 2);

  const uint8_t *p = blob + 7 + palCount * 2;
  const uint8_t *end = blob + size;
  for (uint8_t i = 0; i < nActs && p + 4 <= end; i++) {
    uint8_t id = p[0], w = p[1], h = p[2], nf = p[3];
    p += 4;
    if (id >= PMD_NACTS || nf > 24) { unload(); return false; }
    // valida que ms[] y los datos del frame caben en el blob (archivo truncado)
    uint32_t bytes = (uint32_t)nf * 2 + (uint32_t)w * h * nf;
    if (w == 0 || h == 0 || nf == 0 || p + bytes > end) { unload(); return false; }
    PmdAct &a = acts[id];
    a.w = w;
    a.h = h;
    a.frames = nf;
    for (uint8_t k = 0; k < nf; k++) {
      a.ms[k] = p[0] | (p[1] << 8);
      p += 2;
    }
    a.data = p;
    p += (uint32_t)w * h * nf;
    // fila mas baja con contenido en cualquier frame: anclar por los pies
    uint8_t base = 1;
    for (uint8_t f = 0; f < nf; f++) {
      const uint8_t *fr = a.data + (uint32_t)f * w * h;
      for (int r = h - 1; r >= 0; r--) {
        bool any = false;
        for (int c = 0; c < w && !any; c++)
          if (fr[r * w + c] != 0xFF) any = true;
        if (any) { if (r + 1 > base) base = r + 1; break; }
      }
    }
    a.base = base;
  }
  loaded = true;
  Serial.printf("cargado %s (%u KB)\n", path, size / 1024);
  return true;
}

void PmdMon::unload() {
  if (blob) {
    free(blob);
    blob = nullptr;
  }
  for (auto &a : acts) {
    a.w = a.h = a.frames = a.base = 0;
    a.data = nullptr;
  }
  loaded = false;
}

bool SdThumbs::load(bool verbose) {
  if (!sdReady) return false;
  if (data) {
    free(data);
    data = nullptr;
    count = 0;
    loaded = false;
  }
  uint32_t size = 0;
  data = readSpriteFile("/mons/thumbs.bin", &size, 2UL * 1024 * 1024);
  if (!data) {
    if (verbose) Serial.println("sin thumbs.bin (galeria sin miniaturas)");
    return false;
  }
  if (size < 6 || memcmp(data, "TPTH", 4) != 0) {
    if (verbose) Serial.println("thumbs.bin invalido");
    free(data);
    data = nullptr;
    return false;
  }
  memcpy(&count, data + 4, 2);
  loaded = true;
  if (verbose) Serial.printf("miniaturas cargadas: %u (%u KB)\n", count, size / 1024);
  return true;
}

const uint8_t *SdThumbs::get(int16_t dex) const {
  if (!loaded || dex < 1 || dex > count) return nullptr;
  uint32_t off;
  memcpy(&off, data + 6 + 4 * (dex - 1), 4);
  return data + off;
}

// Which regions actually have their sprite pack stored in internal Flash.
//
// Three probes per region, not one: the realistic failure is a half-finished
// upload of a region pack, and a single probe would call that region present.
// Start, middle and end, so an upload that stopped partway fails the check.
//
// This NARROWS gRegionArt, which starts as everything. If LittleFS fails to
// mount, gRegionArt stays untouched so the built-in fallback sprites still work.
void sdScanRegionArt(bool verbose) {
  if (!sdReady) return;
  uint16_t mask = 0;
  for (uint8_t r = 0; r < REGION_COUNT; r++) {
    if (r == REGION_ALL) continue;      // derived from the others, never probed
    const RegionInfo &rg = REGIONS[r];
    const int16_t probe[3] = { sdRegionProbe(r, 0), sdRegionProbe(r, 1),
                               sdRegionProbe(r, 2) };
    bool all = true;
    for (int i = 0; i < 3 && all; i++) {
      char path[28];
      snprintf(path, sizeof(path), "/mons/p%03u.bin", (unsigned)probe[i]);
      if (!LittleFS.exists(path)) all = false;
    }
    if (all) mask |= (uint16_t)(1u << r);
    if (verbose)
      Serial.printf("art: %-6s %s\n", rg.name, all ? "si" : "NO (falta el pack)");
  }
  gRegionArt = mask;
}

bool sdBegin() {
  sdReady = LittleFS.begin(true /* format on first boot */, "/littlefs", 10, "spiffs");
  if (sdReady) {
    size_t totalKB = LittleFS.totalBytes() / 1024;
    size_t usedKB = LittleFS.usedBytes() / 1024;
    Serial.printf("LittleFS mounted: %u/%u KB used (%u KB free)\n",
                  (unsigned)usedKB, (unsigned)totalKB,
                  (unsigned)(totalKB > usedKB ? totalKB - usedKB : 0));
    if (!LittleFS.exists("/mons")) LittleFS.mkdir("/mons");
    sdScanRegionArt();
  } else {
    Serial.println("LittleFS mount failed (falling back to built-in flash sprites)");
  }
  return sdReady;
}

bool SdMon::load(int16_t dexNum, bool shiny) {
  if (dexNum < 1 || dexNum > 999) return false;
  unload();
  if (!sdReady) return false;

  char path[24];
  uint32_t fsize = 0;
  snprintf(path, sizeof(path), "/mons/%s%03u.bin", shiny ? "s" : "", (unsigned)dexNum);
  uint8_t *raw = readSpriteFile(path, &fsize, 3UL * 1024 * 1024);
  if (!raw && shiny) {  // sin variante shiny: usa la normal
    snprintf(path, sizeof(path), "/mons/%03u.bin", (unsigned)dexNum);
    raw = readSpriteFile(path, &fsize, 3UL * 1024 * 1024);
  }
  if (!raw) {
    Serial.printf("no existe %s\n", path);
    return false;
  }

  if (fsize < 14 || memcmp(raw, "TPK1", 4) != 0) {
    free(raw);
    return false;
  }
  uint16_t header[4];
  memcpy(header, raw + 4, 8);
  w = header[0];
  h = header[1];
  frames = header[2];
  frameMs = header[3];
  memcpy(&palCount, raw + 12, 2);
  // acota dimensiones: evita size desbordado o absurdo con archivo corrupto
  if (palCount > 256 || w == 0 || w > 256 || h == 0 || h > 256 ||
      frames == 0 || frames > 64) {
    free(raw);
    return false;
  }
  uint32_t palBytes = (uint32_t)palCount * 2;
  uint32_t size = (uint32_t)w * h * frames;
  if (14UL + palBytes + size > fsize) {
    Serial.printf("%s truncado (%u de %u)\n", path, fsize, 14U + palBytes + size);
    free(raw);
    return false;
  }
  memcpy(pal, raw + 14, palBytes);

  data = (uint8_t *)ps_malloc(size);
  if (!data) {
    Serial.println("sin PSRAM para el sprite");
    free(raw);
    return false;
  }
  memcpy(data, raw + 14 + palBytes, size);
  free(raw);

  // zoom entero para que el bicho mida ~200 px de alto en pantalla
  scale = 200 / h;
  if (scale < 2) scale = 2;
  if (scale > 5) scale = 5;

  Serial.printf("cargado %s: %ux%u x%u frames @%ums, escala %u\n",
                path, w, h, frames, frameMs, scale);
  loaded = true;
  return true;
}

void SdMon::unload() {
  if (data) {
    free(data);
    data = nullptr;
  }
  loaded = false;
}

// ---------------------------------------------------------------------------
// USB serial upload protocol (writes compressed or raw sprites to LittleFS):
//   PUT <path> <bytes>\n  + raw payload    -> "OK" ... "#" ... "DONE"
//   LS\n                                   -> list /mons + "DONE"
//   DF\n                                   -> total/used/free bytes + "DONE"
// Used by web/index.html and tools/send_sd.py.
// ---------------------------------------------------------------------------

bool sdSerialCommand(const String &line) {
  if (line.startsWith("PUT ")) {
    int sp = line.lastIndexOf(' ');
    if (sp <= 4) {
      Serial.println("ERR");
      return true;
    }
    String path = line.substring(4, sp);
    uint32_t size = line.substring(sp + 1).toInt();
    if (!sdReady || size == 0 || size > 4 * 1024 * 1024) {
      Serial.println("ERR");
      return true;
    }
    if (!path.startsWith("/")) path = "/" + path;
    if (path.startsWith("/mons/") && !LittleFS.exists("/mons")) {
      LittleFS.mkdir("/mons");
    }
    File f = LittleFS.open(path, FILE_WRITE);
    if (!f) {
      Serial.println("ERR");
      return true;
    }
    Serial.println("OK");
    static uint8_t buf[2048];
    uint32_t remaining = size;
    bool writeOk = true;
    Serial.setTimeout(5000);
    while (remaining > 0) {
      size_t want = remaining > sizeof(buf) ? sizeof(buf) : remaining;
      size_t n = Serial.readBytes(buf, want);
      if (n == 0) { writeOk = false; break; }  // timeout
      if (f.write(buf, n) != n) { writeOk = false; break; }  // flash full / write error
      remaining -= n;
      Serial.println("#");  // ack: ready for next chunk
    }
    f.close();
    Serial.setTimeout(1000);
    bool ok = writeOk && remaining == 0;
    if (!ok) {
      LittleFS.remove(path);  // do not leave a truncated file on Flash
    } else {
      sdDirty = true;
      // A pack file just landed, so which regions are playable may have changed.
      // Flag it rather than rescanning here: this runs between the last data block
      // and the DONE the host is waiting on. loop() picks it up quietly.
      sdArtDirty = true;
      if (path == "/mons/thumbs.bin") sdThumbsDirty = true;
    }
    Serial.println(ok ? "DONE" : "ERR");
    return true;
  } else if (line == "LS") {
    File dir = LittleFS.open("/mons");
    if (dir) {
      File e;
      while ((e = dir.openNextFile())) {
        Serial.printf("%s %u\n", e.name(), (uint32_t)e.size());
        e.close();
      }
      dir.close();
    }
    Serial.println("DONE");
    return true;
  } else if (line == "DF") {
    if (sdReady) {
      size_t total = LittleFS.totalBytes();
      size_t used = LittleFS.usedBytes();
      Serial.printf("%u %u %u\n", (unsigned)total, (unsigned)used,
                    (unsigned)(total > used ? total - used : 0));
    }
    Serial.println("DONE");
    return true;
  }
  return false;
}
