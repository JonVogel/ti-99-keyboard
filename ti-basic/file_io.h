/*
 * TI BASIC Interpreter — File I/O
 *
 * Implements OPEN / CLOSE / PRINT # / INPUT # / LINPUT # / EOF() against
 * a small handle table routed to either LittleFS (FLASH1.) or SD card
 * (DSK1.). Scope: DISPLAY / SEQUENTIAL format only. INTERNAL / RELATIVE
 * / VARIABLE / FIXED are recognized in OPEN parsing but not supported;
 * OPEN returns an error if they're requested.
 *
 * Unit numbers 1..MAX_FILES (inclusive). Unit 0 is reserved.
 */

#ifndef FILE_IO_H
#define FILE_IO_H

#include <Arduino.h>
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>
#include <FS.h>
#include "dsk_image.h"

namespace fio
{
  // User units 1..4 + two reserved for internal COPY source/dest.
  static const int MAX_FILES = 6;
  static const int MAX_DSK   = 35;   // DSK1..DSK9, DSKA..DSKZ

  // Convert a TI drive character to a 1-based slot index.
  //   '1'..'9' -> 1..9
  //   'A'..'Z' / 'a'..'z' -> 10..35
  //   anything else -> 0
  inline int driveFromChar(char c)
  {
    if (c >= '1' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return 10 + (c - 'A');
    if (c >= 'a' && c <= 'z') return 10 + (c - 'a');
    return 0;
  }

  inline char driveToChar(int drive)
  {
    if (drive >= 1 && drive <= 9)  return (char)('0' + drive);
    if (drive >= 10 && drive <= 35) return (char)('A' + drive - 10);
    return '?';
  }

  enum Device : uint8_t { DEV_NONE = 0, DEV_FLASH = 1, DEV_SD = 2, DEV_DSK = 3 };
  enum Mode   : uint8_t { MODE_INPUT = 0, MODE_OUTPUT = 1, MODE_APPEND = 2, MODE_UPDATE = 3 };

  struct Slot
  {
    bool    inUse = false;
    Device  device = DEV_NONE;
    Mode    mode   = MODE_INPUT;
    File    fh;
    char    path[48] = {0};
    bool    eof = false;   // sticky end-of-file for INPUT
    // When device == DEV_DSK, these drive the V9T9 reader/writer
    // instead of fh.
    int     dskDrive = 0;                 // 1..MAX_DSK
    dsk::DskImage::DisVarReader dskRdr{};
    dsk::DskImage::DisVarWriter dskWtr{};
    bool    dskWriting = false;
  };

  inline Slot g_slots[MAX_FILES + 1];   // index 1..MAX_FILES; [0] unused
  inline bool g_sdOk = false;

  // Mount table: DSK1..DSK3. Each slot points to a DskImage backed by
  // either LittleFS or the SD card. Unmounted slots have
  // isOpen() == false.
  struct Mount
  {
    dsk::DskImage img;
    // Display form of the source, e.g. "FLASH.SYSTEM.DSK" — used for
    // persistence and for the DIR header. Blank when unmounted.
    char     spec[48]      = {0};
    bool     mounted       = false;
    bool     fromFlash     = false;   // true=LittleFS, false=SD
    char     fsPath[48]    = {0};     // resolved path on the chosen fs
  };
  inline Mount g_mounts[MAX_DSK + 1];   // index 1..MAX_DSK; [0] unused

  // Parse a mount spec into (device, fsPath). Accepts:
  //   FLASH.NAME[.DSK]   -> fsPath = "/NAME.DSK" on LittleFS
  //   SDCARD.NAME[.DSK]  -> fsPath = "/NAME.DSK" on SD
  //   /ABS/PATH.DSK      -> fsPath = verbatim on SD (legacy)
  //   NAME[.DSK]         -> fsPath = "/NAME.DSK" on SD (legacy)
  // Returns true on success.
  inline bool resolveMountSpec(const char* spec, bool& fromFlash,
                               char* fsPath, int fsPathSize)
  {
    if (!spec) return false;
    const char* body = spec;
    if (strncasecmp(spec, "FLASH.", 6) == 0)
    {
      fromFlash = true; body = spec + 6;
    }
    else if (strncasecmp(spec, "FLASH1.", 7) == 0)
    {
      fromFlash = true; body = spec + 7;
    }
    else if (strncasecmp(spec, "SDCARD.", 7) == 0)
    {
      fromFlash = false; body = spec + 7;
    }
    else if (spec[0] == '/')
    {
      fromFlash = false;
      snprintf(fsPath, fsPathSize, "%s", spec);
      return true;
    }
    else
    {
      fromFlash = false;
      body = spec;
    }
    // Auto-append .DSK if no extension
    bool hasDot = strchr(body, '.') != NULL;
    snprintf(fsPath, fsPathSize, "/%s%s", body, hasDot ? "" : ".DSK");
    return true;
  }

  inline bool mountDskImage(int drive, const char* spec)
  {
    if (drive < 1 || drive > MAX_DSK) return false;
    Mount& m = g_mounts[drive];
    if (m.mounted) m.img.close();

    bool fromFlash;
    char fsPath[48];
    if (!resolveMountSpec(spec, fromFlash, fsPath, sizeof(fsPath)))
    {
      m.mounted = false; m.spec[0] = '\0';
      return false;
    }
    if (!fromFlash && !g_sdOk)
    {
      m.mounted = false; m.spec[0] = '\0';
      return false;
    }
    fs::FS& fs = fromFlash ? (fs::FS&)LittleFS : (fs::FS&)SD;
    if (!m.img.open(fs, fsPath))
    {
      m.mounted = false; m.spec[0] = '\0';
      return false;
    }
    m.mounted = true;
    m.fromFlash = fromFlash;
    snprintf(m.spec, sizeof(m.spec), "%s", spec);
    snprintf(m.fsPath, sizeof(m.fsPath), "%s", fsPath);
    return true;
  }

  inline void unmountDskImage(int drive)
  {
    if (drive < 1 || drive > MAX_DSK) return;
    Mount& m = g_mounts[drive];
    if (m.mounted) m.img.close();
    m.mounted = false;
    m.spec[0] = '\0';
    m.fsPath[0] = '\0';
  }

  inline dsk::DskImage* dskImage(int drive)
  {
    if (drive < 1 || drive > MAX_DSK) return nullptr;
    Mount& m = g_mounts[drive];
    return m.mounted ? &m.img : nullptr;
  }

  inline const char* dskImagePath(int drive)
  {
    if (drive < 1 || drive > MAX_DSK) return "";
    return g_mounts[drive].spec;
  }

  // Try to bring up the SD card. Caller should have already configured
  // the SPI bus. Returns true on success.
  inline bool beginSD(int cs = 10, int sck = 12, int miso = 13, int mosi = 11)
  {
    SPI.begin(sck, miso, mosi, cs);
    g_sdOk = SD.begin(cs, SPI);
    return g_sdOk;
  }

  // Parse "DEVICE.NAME" into (device, path, dskDrive). outPath is the
  // device-local name (without the slash prefix for DSK); dskDrive
  // is 1..3 when device == DEV_DSK, else 0.
  inline bool parseSpec(const char* spec, Device& dev, char* outPath,
                        int outSize, int& dskDrive)
  {
    if (!spec) return false;
    const char* dot = strchr(spec, '.');
    if (!dot) return false;
    int prefixLen = (int)(dot - spec);
    dskDrive = 0;
    // FLASH.NAME (canonical) and FLASH1.NAME (legacy alias) both map to
    // internal LittleFS.
    if ((prefixLen == 5 && strncasecmp(spec, "FLASH", 5) == 0) ||
        (prefixLen == 6 && strncasecmp(spec, "FLASH1", 6) == 0))
    {
      dev = DEV_FLASH;
      snprintf(outPath, outSize, "/%s", dot + 1);
      return true;
    }
    if (prefixLen == 6 && strncasecmp(spec, "SDCARD", 6) == 0)
    {
      dev = DEV_SD;
      snprintf(outPath, outSize, "/%s", dot + 1);
      return true;
    }
    if (prefixLen == 4 && strncasecmp(spec, "DSK", 3) == 0)
    {
      int d = driveFromChar(spec[3]);
      if (d > 0)
      {
        dev = DEV_DSK;
        dskDrive = d;
        snprintf(outPath, outSize, "%s", dot + 1);
        return true;
      }
    }
    return false;
  }

  inline bool unitValid(int unit) { return unit >= 1 && unit <= MAX_FILES; }

  // OPEN: returns 0 on success, non-zero error code otherwise.
  // err=1: bad unit, 2: already open, 3: bad spec, 4: device unavailable,
  // 5: unsupported mode, 6: file system error
  inline int openFile(int unit, const char* spec, Mode mode)
  {
    if (!unitValid(unit)) return 1;
    Slot& s = g_slots[unit];
    if (s.inUse) return 2;

    Device dev;
    char path[48];
    int drive = 0;
    if (!parseSpec(spec, dev, path, sizeof(path), drive)) return 3;

    if (dev == DEV_DSK)
    {
      dsk::DskImage* img = dskImage(drive);
      if (!img) return 4;
      if (mode == MODE_OUTPUT)
      {
        if (img->readOnly()) return 5;
        if (!img->openDisVarWriter(path, s.dskWtr)) return 6;
        s.inUse = true; s.device = DEV_DSK; s.mode = mode;
        s.eof = false; s.dskDrive = drive; s.dskWriting = true;
        snprintf(s.path, sizeof(s.path), "%s", path);
        return 0;
      }
      if (mode == MODE_INPUT)
      {
        dsk::FileInfo info;
        if (!img->findFile(path, info)) return 6;
        // Refuse PROGRAM / INTERNAL files in Phase 1 — only DIS/VAR.
        if (info.flags & 0x01) return 5;    // PROGRAM
        if (info.flags & 0x02) return 5;    // INTERNAL
        s.inUse = true; s.device = DEV_DSK; s.mode = mode;
        s.eof = false; s.dskDrive = drive; s.dskWriting = false;
        snprintf(s.path, sizeof(s.path), "%s", path);
        if (!img->openDisVarReader(info, s.dskRdr)) s.eof = true;
        return 0;
      }
      return 5;   // APPEND / UPDATE on DSK not implemented in Phase 2
    }

    const char* openMode = "r";
    if (mode == MODE_OUTPUT)      openMode = "w";
    else if (mode == MODE_APPEND) openMode = "a";
    else if (mode == MODE_UPDATE) openMode = "r+";

    File fh;
    if (dev == DEV_FLASH)
    {
      fh = LittleFS.open(path, openMode);
    }
    else
    {
      fh = SD.open(path, openMode);
    }
    if (!fh) return 6;

    s.inUse = true;
    s.device = dev;
    s.mode = mode;
    s.fh = fh;
    s.eof = false;
    snprintf(s.path, sizeof(s.path), "%s", path);
    return 0;
  }

  inline int closeFile(int unit)
  {
    if (!unitValid(unit)) return 1;
    Slot& s = g_slots[unit];
    if (!s.inUse) return 0;   // closing an unopen unit is a no-op
    if (s.device == DEV_DSK)
    {
      if (s.dskWriting)
      {
        dsk::DskImage* img = dskImage(s.dskDrive);
        if (img) img->closeDisVarWriter(s.dskWtr);
      }
    }
    else
    {
      s.fh.close();
    }
    s.inUse = false;
    s.device = DEV_NONE;
    s.eof = false;
    s.dskDrive = 0;
    s.dskWriting = false;
    s.path[0] = '\0';
    return 0;
  }

  inline int printLineTo(int unit, const char* text)
  {
    if (!unitValid(unit) || !g_slots[unit].inUse) return 1;
    Slot& s = g_slots[unit];
    if (s.mode == MODE_INPUT) return 5;

    if (s.device == DEV_DSK)
    {
      dsk::DskImage* img = dskImage(s.dskDrive);
      if (!img) return 4;
      return img->writeDisVarRecord(s.dskWtr, text) ? 0 : 6;
    }

    s.fh.print(text);
    s.fh.print('\n');
    s.fh.flush();
    return 0;
  }

  // Read a line (LF-terminated) into buf. Returns 0 on success, non-zero
  // on EOF or error. Strips trailing \r\n.
  inline int readLineFrom(int unit, char* buf, int bufSize)
  {
    if (!unitValid(unit) || !g_slots[unit].inUse) return 1;
    Slot& s = g_slots[unit];
    if (s.mode != MODE_INPUT && s.mode != MODE_UPDATE) return 5;

    if (s.device == DEV_DSK)
    {
      dsk::DskImage* img = dskImage(s.dskDrive);
      if (!img) { s.eof = true; buf[0] = '\0'; return 2; }
      int len = img->readDisVarRecord(s.dskRdr, buf, bufSize);
      if (len < 0) { s.eof = true; buf[0] = '\0'; return 2; }
      if (s.dskRdr.eof) s.eof = true;
      return 0;
    }

    if (!s.fh.available())
    {
      s.eof = true;
      buf[0] = '\0';
      return 2;
    }
    int n = 0;
    while (s.fh.available() && n < bufSize - 1)
    {
      int c = s.fh.read();
      if (c < 0) break;
      if (c == '\n') break;
      if (c == '\r') continue;
      buf[n++] = (char)c;
    }
    buf[n] = '\0';
    if (!s.fh.available()) s.eof = true;
    return 0;
  }

  inline bool isEof(int unit)
  {
    if (!unitValid(unit)) return true;
    Slot& s = g_slots[unit];
    if (!s.inUse) return true;
    if (s.device == DEV_DSK) return s.eof || s.dskRdr.eof;
    return s.eof || !s.fh.available();
  }

  inline void closeAll()
  {
    for (int i = 1; i <= MAX_FILES; i++) closeFile(i);
  }
}

#endif // FILE_IO_H
