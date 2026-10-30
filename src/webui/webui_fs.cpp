//
// webui_fs.cpp
//
// BMC64 - A bare metal Commodore emulator for Raspberry Pi
// Copyright (C) 2026 aminch <https://github.com/aminch>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//
// Modified for BMA800XL from BMC64's src/webui/webui_fs.cpp: protects the
// Atari emulator's configuration files; BMC64's profile and keyboard map
// files are removed. Uploads fill a 64 KB buffer before each SD write and
// log their speed.
//
// Modified for BMCoco: CoCo files.
//
// Modified for BMAmiga.
//

#include "webui_fs.h"
#include "webui.h"

#if BMC64_WEBUI

#include "webui_http.h"
#include "webui_machine.h"

#include <circle/logger.h>
#include <circle/net/socket.h>
#include <circle/netdevice.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/types.h>

#include <stdio.h>
#include <string.h>

#include "../host.h"
#include "../sdcard/sd_fs.h"

// Implemented in src/vice_network.cpp.
extern "C" const char *circle_get_disk_volume(void);
// Queues a file for the emulator main loop to autostart (interrupt safe;
// see third_party/common/ui.c).
extern "C" void emu_autostart_interrupt(const char *path);

using webhttp::CChunkedResponse;

namespace {

// The volume a request is for (ResolveTarget()): "" for the card's own
// (circle_get_disk_volume()), or "SD2", the card's second partition
// (host_volume2()), whose paths go to sd_fs with its name ("SD2:/x").
char g_volume[8] = "";

// PATH (a sandboxed path, "/x") on the request's volume, for sd_fs. Four
// at a time (a rename has two).
const char *V(const char *path) {
  static char paths[4][600];
  static unsigned next;
  if (g_volume[0] == '\0') {
    return path;
  }
  char *out = paths[next++ % 4];
  snprintf(out, sizeof(paths[0]), "%s:%s", g_volume, path);
  return out;
}

// The request's volume, as the client names it.
const char *VolumeName(void) {
  return g_volume[0] != '\0' ? g_volume : circle_get_disk_volume();
}

const unsigned WEBUI_FS_MAX_ENTRIES = 6000;
// Matches Circle's TCP receive window (see circle_tcp_window_patch.diff), so
// one upload batch can take a whole window of segments.
const unsigned WEBUI_FS_IO_CHUNK = 64 * 1024;
// Largest file the editor will open or save (the config files are a few KB;
// the body is streamed to a temp file, so this is not a RAM limit).
const unsigned WEBUI_FS_EDIT_MAX = 256 * 1024;

// One request is handled at a time by the web UI task, so a single file
// scratch buffer is safe and keeps it off the task stack.
u8 s_io_buffer[WEBUI_FS_IO_CHUNK];

// Write s as a JSON string literal (with surrounding quotes), escaping
// control characters. Names are UTF-8 on the card (FatFs's API is UTF-8,
// src/patches/circle_fatfs_utf8_patch.diff), as JSON wants them.
void JsonString(CChunkedResponse *r, const char *s) {
  r->Write("\"");
  for (const unsigned char *p = (const unsigned char *) s; *p != '\0'; p++) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') {
      char esc[2] = {'\\', (char) c};
      r->WriteN(esc, 2);
    } else if (c == '\n') {
      r->Write("\\n");
    } else if (c == '\r') {
      r->Write("\\r");
    } else if (c == '\t') {
      r->Write("\\t");
    } else if (c < 0x20) {
      r->Printf("\\u%04x", c);
    } else {
      char ch = (char) c;
      r->WriteN(&ch, 1);
    }
  }
  r->Write("\"");
}

void WriteDateTime(CChunkedResponse *r, const sd_time *t) {
  if (t->year == 0) {
    return;  // unknown timestamp -> empty string
  }
  r->Printf("%04u-%02u-%02uT%02u:%02u:%02u", t->year, t->month, t->day,
            t->hour, t->minute, t->second);
}

// Parse "YYYY-MM-DDTHH:MM:SS" (local wall-clock time, as WriteDateTime
// emits). Returns FALSE if malformed or outside what FAT can store
// (1980..2107).
boolean ParseDateTime(const char *s, sd_time *t) {
  unsigned year, month, day, hour, minute, second;
  if (sscanf(s, "%4u-%2u-%2uT%2u:%2u:%2u", &year, &month, &day, &hour, &minute,
             &second) != 6) {
    return FALSE;
  }
  if (year < 1980 || year > 2107 || month < 1 || month > 12 || day < 1 ||
      day > 31 || hour > 23 || minute > 59 || second > 59) {
    return FALSE;
  }
  t->year = (uint16_t) year;
  t->month = (uint8_t) month;
  t->day = (uint8_t) day;
  t->hour = (uint8_t) hour;
  t->minute = (uint8_t) minute;
  t->second = (uint8_t) second;
  return TRUE;
}

// Normalise a client path into a sandboxed absolute path within a volume.
// Returns 0 on success (out starts with '/'), -1 if the path is unsafe.
int SanitizeRelPath(const char *in, char *out, unsigned out_size) {
  if (out_size < 2) {
    return -1;
  }
  if (in == 0 || in[0] == '\0') {
    strcpy(out, "/");
    return 0;
  }
  if (in[0] != '/') {
    return -1;
  }

  unsigned o = 1;
  out[0] = '/';
  const char *p = in;
  while (*p != '\0') {
    while (*p == '/') {
      p++;
    }
    const char *seg = p;
    while (*p != '\0' && *p != '/') {
      p++;
    }
    unsigned seg_len = (unsigned) (p - seg);
    if (seg_len == 0) {
      continue;
    }
    if (seg_len > SD_MAX_NAME_LEN) {
      return -1;
    }
    if (seg_len == 1 && seg[0] == '.') {
      continue;
    }
    if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
      return -1;
    }
    for (unsigned i = 0; i < seg_len; i++) {
      unsigned char c = (unsigned char) seg[i];
      if (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' ||
          c == '"' || c == '<' || c == '>' || c == '|') {
        return -1;
      }
    }
    if (o > 1) {
      if (o + 1 >= out_size) {
        return -1;
      }
      out[o++] = '/';
    }
    if (o + seg_len >= out_size) {
      return -1;
    }
    memcpy(out + o, seg, seg_len);
    o += seg_len;
  }
  out[o] = '\0';
  return 0;
}

// Resolve and validate the vol/path query parameters. Returns 0 on
// success and fills clean, a path on the card for sd_fs.h; on failure it has
// already sent an error response and returns -1.
int ResolveTarget(CSocket *socket, const char *query, char *clean,
                  unsigned clean_size, boolean require_file) {
  char vol[24];
  char raw[560];
  const char *disk = circle_get_disk_volume();

  if (!webhttp::QueryParam(query, "vol", vol, sizeof(vol)) || vol[0] == '\0') {
    strncpy(vol, disk, sizeof(vol) - 1);
    vol[sizeof(vol) - 1] = '\0';
  }
  if (strcmp(vol, disk) == 0) {
    g_volume[0] = '\0';
  } else if (strcmp(vol, "SD2") == 0 && host_volume2() != 0) {
    strcpy(g_volume, "SD2");
  } else {
    webhttp::SendText(socket, 404, "Not Found", "unknown volume\n");
    return -1;
  }
  if (!webhttp::QueryParam(query, "path", raw, sizeof(raw))) {
    if (query != 0 &&
        (strncmp(query, "path=", 5) == 0 || strstr(query, "&path=") != 0)) {
      webhttp::SendText(socket, 400, "Bad Request", "path too long or invalid\n");
      return -1;
    }
    strcpy(raw, "/");
  }
  if (SanitizeRelPath(raw, clean, clean_size) != 0) {
    webhttp::SendText(socket, 400, "Bad Request", "bad path\n");
    return -1;
  }
  if (require_file && strcmp(clean, "/") == 0) {
    webhttp::SendText(socket, 400, "Bad Request", "not a file\n");
    return -1;
  }
  return 0;
}

boolean CiEqual(const char *a, const char *b) {
  for (; *a != '\0' && *b != '\0'; a++, b++) {
    char ca = *a;
    char cb = *b;
    if (ca >= 'A' && ca <= 'Z') ca = (char) (ca + 32);
    if (cb >= 'A' && cb <= 'Z') cb = (char) (cb + 32);
    if (ca != cb) return FALSE;
  }
  return *a == *b;
}

// Only the image/program types this machine's Autostart accepts as-is
// (webui_machine.cpp).
boolean IsAutostartable(const char *clean) {
  const char *dot = 0;
  for (const char *p = clean; *p != '\0'; p++) {
    if (*p == '/') dot = 0;
    else if (*p == '.') dot = p + 1;
  }
  return dot != 0 && WebUiMachineCanRun(dot);
}

// The boot configuration files. Upload and delete refuse them (in any
// folder); the editor's save endpoint is the only way the web UI changes them.
const char *const kConfigFiles[] = {
    "wpa_supplicant.conf", "cmdline.txt", "config.txt",
};

boolean IsConfigFile(const char *name) {
  for (unsigned i = 0; i < sizeof(kConfigFiles) / sizeof(kConfigFiles[0]); i++) {
    if (CiEqual(name, kConfigFiles[i])) return TRUE;
  }
  return FALSE;
}

// bmamiga.cfg (the BMAmiga menu's settings) is editable too, but (unlike
// the files above) stays uploadable so a prepared copy can still be
// dropped on the card.
boolean IsEditableName(const char *name) {
  return IsConfigFile(name) || CiEqual(name, "bmamiga.cfg");
}

// The config files are editable only in the volume root.
boolean IsEditablePath(const char *clean) {
  if (clean[0] != '/' || g_volume[0] != '\0') return FALSE;
  const char *base = strrchr(clean, '/') + 1;
  return base == clean + 1 && IsEditableName(base);
}

// Uploads must not clobber the boot configuration or the Wi-Fi firmware
// directory.
boolean IsProtectedPath(const char *clean) {
  // The boot files are the card's own volume's (not the second partition's).
  if (g_volume[0] != '\0') return FALSE;
  const char *seg = clean;
  while (*seg == '/') seg++;
  const char *seg_end = seg;
  while (*seg_end != '\0' && *seg_end != '/') seg_end++;
  if ((unsigned) (seg_end - seg) == 8) {
    char first[9];
    memcpy(first, seg, 8);
    first[8] = '\0';
    if (CiEqual(first, "firmware")) return TRUE;
  }

  const char *base = clean;
  for (const char *p = clean; *p != '\0'; p++) {
    if (*p == '/') base = p + 1;
  }
  return IsConfigFile(base);
}

// A name typed into New folder / Rename: one path segment that FAT stores
// exactly as written (in UTF-8, as FatFs's API is). FatFs would silently
// trim a trailing dot or space, so those are refused rather than creating
// a different name than the one asked for.
boolean IsValidEntryName(const char *name) {
  size_t length = strlen(name);
  if (length == 0 || length > SD_MAX_NAME_LEN) return FALSE;
  if (name[0] == ' ' || name[length - 1] == ' ' || name[length - 1] == '.') {
    return FALSE;
  }
  for (const char *p = name; *p != '\0'; p++) {
    unsigned char c = (unsigned char) *p;
    if (c < 0x20 || c == 0x7F || c == '/' || c == '\\' || c == ':' ||
        c == '*' || c == '?' || c == '"' || c == '<' || c == '>' ||
        c == '|') {
      return FALSE;
    }
  }
  return TRUE;
}

const char *const kBadNameText =
    "invalid name: no / \\ : * ? \" < > |, and no "
    "leading space or trailing space or dot\n";

enum BodyResult {
  BODY_OK,
  BODY_CREATE_FAILED,
  BODY_WRITE_FAILED,  // write error (disk full?)
  BODY_TRUNCATED,     // client aborted or timed out
  BODY_HAS_NUL,       // only when reject_nul is set
};

// Stream a request body into a new file at temppath: prefetched bytes (read
// with the headers) first, then the rest from the socket, `total` bytes in
// all. On anything but BODY_OK the temp file is removed.
BodyResult ReceiveBodyToFile(CSocket *socket, const char *temppath,
                             const unsigned char *prefetched,
                             unsigned prefetched_len, unsigned long total,
                             boolean reject_nul) {
  sd_file *file = sd_open(V(temppath), SD_WRITE);
  if (file == 0) {
    return BODY_CREATE_FAILED;
  }

  unsigned long written = 0;
  BodyResult result = BODY_OK;

  if (prefetched_len > total) {
    prefetched_len = (unsigned) total;
  }
  if (prefetched_len > 0) {
    if (reject_nul && memchr(prefetched, 0, prefetched_len) != 0) {
      result = BODY_HAS_NUL;
    } else if (sd_write(file, prefetched, prefetched_len) != SD_OK) {
      result = BODY_WRITE_FAILED;
    }
    written += prefetched_len;
  }

  // Circle returns one TCP segment per Receive, so fill the whole buffer
  // before writing: a blocking Receive on an empty queue lets the other
  // tasks run, and the SD card gets a few large writes instead of many
  // small ones (no task, including the network, runs during a write).
  // A Receive into less space than the segment drops the rest of it, so
  // only ask for more while a whole frame still fits (or the body ends).
  // From BMC64's src/webui/webui_fs.cpp.
  u64 start_ticks = CTimer::GetClockTicks64();
  unsigned long batches = 0;
  u64 wait_ticks = 0;
  u64 write_ticks = 0;
  u64 yield_ticks = 0;
  while (result == BODY_OK && written < total) {
    unsigned fill = 0;
    for (;;) {
      unsigned long remain = total - written - fill;
      unsigned space = sizeof(s_io_buffer) - fill;
      if (remain == 0 || (space < FRAME_BUFFER_SIZE && remain > space)) {
        break;
      }
      unsigned want = remain < space ? (unsigned) remain : space;
      u64 t0 = CTimer::GetClockTicks64();
      int n = socket->Receive(s_io_buffer + fill, want, 0);
      wait_ticks += CTimer::GetClockTicks64() - t0;
      if (n <= 0) {
        result = BODY_TRUNCATED;
        break;
      }
      fill += (unsigned) n;
    }
    if (result != BODY_OK) {
      break;
    }
    if (reject_nul && memchr(s_io_buffer, 0, fill) != 0) {
      result = BODY_HAS_NUL;
      break;
    }
    u64 t1 = CTimer::GetClockTicks64();
    if (sd_write(file, s_io_buffer, fill) != SD_OK) {
      result = BODY_WRITE_FAILED;
      break;
    }
    u64 t2 = CTimer::GetClockTicks64();
    write_ticks += t2 - t1;
    written += fill;
    batches++;
    CScheduler::Get()->Yield();
    yield_ticks += CTimer::GetClockTicks64() - t2;
  }

  // Throughput, the average batch size and where the time went show what
  // limits an upload: waiting for data (the network; other tasks also run
  // while it waits), SD writes, or other tasks between batches.
  if (result == BODY_OK && batches > 0) {
    const u64 per_ms = CLOCKHZ / 1000;
    u64 ms = (CTimer::GetClockTicks64() - start_ticks) / per_ms;
    CLogger::Get()->Write("webui", LogNotice,
                          "Received %lu bytes in %lu ms (%lu KB/s), "
                          "%lu batches of %lu bytes avg; "
                          "waiting %lu ms, SD writes %lu ms, other tasks %lu ms",
                          total, (unsigned long) ms,
                          ms > 0 ? (unsigned long) (total / ms) : 0UL,
                          batches, (unsigned long) (total / batches),
                          (unsigned long) (wait_ticks / per_ms),
                          (unsigned long) (write_ticks / per_ms),
                          (unsigned long) (yield_ticks / per_ms));
  }

  if (sd_close(file) != SD_OK && result == BODY_OK) {
    result = BODY_WRITE_FAILED;
  }
  if (result != BODY_OK) {
    sd_unlink(V(temppath));
  }
  return result;
}

void SendBodyFailure(CSocket *socket, BodyResult result) {
  switch (result) {
  case BODY_CREATE_FAILED:
    webhttp::SendText(socket, 500, "Internal Server Error",
                      "cannot create file\n");
    break;
  case BODY_WRITE_FAILED:
    webhttp::SendText(socket, 507, "Insufficient Storage",
                      "write failed (disk full?)\n");
    break;
  case BODY_HAS_NUL:
    webhttp::SendText(socket, 400, "Bad Request", "not a text file\n");
    break;
  default:
    webhttp::SendText(socket, 400, "Bad Request", "upload truncated\n");
    break;
  }
}

// Lets other tasks run while a big folder is deleted.
void YieldWhileDeleting(void *ctx) {
  (void) ctx;
  CScheduler::Get()->Yield();
}

// ---- listing a folder ----

struct ListState {
  CChunkedResponse *r;
  const char *clean;
  boolean at_root;
  boolean first;
  boolean truncated;
  unsigned count;
};

int ListEntry(void *ctx, const sd_info *info) {
  ListState *state = (ListState *) ctx;
  CChunkedResponse *r = state->r;
  if (state->count >= WEBUI_FS_MAX_ENTRIES) {
    state->truncated = TRUE;
    return 1;
  }
  if (!state->first) {
    r->Write(",");
  }
  state->first = FALSE;

  r->Write("{\"name\":");
  JsonString(r, info->name);
  r->Printf(",\"size\":%llu,\"dir\":%s,\"mtime\":\"",
            (unsigned long long) info->size, info->is_dir ? "true" : "false");
  WriteDateTime(r, &info->mtime);
  r->Write("\"");
  char child[560];
  boolean child_ok = (unsigned) snprintf(child, sizeof(child), "%s%s%s",
                                         state->clean,
                                         state->at_root ? "" : "/",
                                         info->name) < sizeof(child);
  if (child_ok && !info->is_dir && info->size <= WEBUI_FS_EDIT_MAX &&
      IsEditablePath(child)) {
    r->Write(",\"edit\":true");
  }
  // Entries the web UI won't rename or delete, so the page can leave
  // those actions out of the row's menu.
  if (child_ok && IsProtectedPath(child)) {
    r->Write(",\"protected\":true");
  }
  r->Write("}");

  state->count++;
  if ((state->count & 63) == 0) {
    CScheduler::Get()->Yield();
  }
  return 0;
}

}  // namespace

void WebUiFsVolumes(CSocket *socket) {
  const char *vol = circle_get_disk_volume();
  uint32_t total_kb = 0;
  uint32_t free_kb = 0;
  int rc = sd_space_kb(&total_kb, &free_kb);

  CChunkedResponse r(socket, 200, "OK", "application/json");
  r.Write("{\"volumes\":[");
  if (rc == SD_OK) {
    r.Write("{\"id\":");
    JsonString(&r, vol);
    r.Printf(",\"kind\":\"sdcard\",\"total_kb\":%lu,\"free_kb\":%lu}",
             (unsigned long) total_kb, (unsigned long) free_kb);
  }
  // The card's second partition (an exFAT one for big disk images).
  if (host_volume2() != 0 &&
      sd_volume_space_kb("SD2", &total_kb, &free_kb) == SD_OK) {
    r.Write(rc == SD_OK ? ",{\"id\":\"SD2\"" : "{\"id\":\"SD2\"");
    r.Printf(",\"kind\":\"partition\",\"total_kb\":%lu,\"free_kb\":%lu}",
             (unsigned long) total_kb, (unsigned long) free_kb);
  }
  r.Write("]}");
  r.Finish();
}

void WebUiFsList(CSocket *socket, const char *query) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), FALSE) != 0) {
    return;
  }

  // The root always exists; anything else must be a folder.
  if (strcmp(clean, "/") != 0) {
    sd_info info;
    int rc = sd_stat_info(V(clean), &info);
    if (rc == SD_NOT_FOUND || rc == SD_INVALID || (rc == SD_OK && !info.is_dir)) {
      webhttp::SendText(socket, 404, "Not Found", "no such directory\n");
      return;
    }
    if (rc != SD_OK) {
      webhttp::SendText(socket, 500, "Internal Server Error",
                        "cannot open directory\n");
      return;
    }
  }

  CChunkedResponse r(socket, 200, "OK", "application/json");
  r.Write("{\"vol\":");
  JsonString(&r, VolumeName());
  r.Write(",\"path\":");
  JsonString(&r, clean);
  r.Write(",\"entries\":[");

  ListState state;
  state.r = &r;
  state.clean = clean;
  state.at_root = strcmp(clean, "/") == 0;
  state.first = TRUE;
  state.truncated = FALSE;
  state.count = 0;
  sd_list_info(V(clean), ListEntry, &state);

  r.Printf("],\"truncated\":%s}", state.truncated ? "true" : "false");
  r.Finish();
}

void WebUiFsDownload(CSocket *socket, const char *query) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }

  sd_file *file = sd_open(V(clean), SD_READ);
  if (file == 0) {
    webhttp::SendText(socket, 404, "Not Found", "cannot open file\n");
    return;
  }
  uint64_t size = sd_size(file);

  const char *base = clean;
  for (const char *p = clean; *p != '\0'; p++) {
    if (*p == '/') {
      base = p + 1;
    }
  }
  char safe_name[128];
  unsigned si = 0;
  for (const char *p = base; *p != '\0' && si + 1 < sizeof(safe_name); p++) {
    unsigned char c = (unsigned char) *p;
    if (c == '"' || c == '\\' || c < 0x20) {
      continue;
    }
    safe_name[si++] = (char) c;
  }
  safe_name[si] = '\0';
  if (si == 0) {
    strcpy(safe_name, "download");
  }

  char header[400];
  int hn = snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "Content-Length: %llu\r\n"
                    "Content-Disposition: attachment; filename=\"%s\"\r\n"
                    "Connection: close\r\n"
                    "Cache-Control: no-store\r\n"
                    "\r\n",
                    (unsigned long long) size, safe_name);
  if (hn <= 0 || (unsigned) hn >= sizeof(header) ||
      !webhttp::SendAll(socket, header, (unsigned) hn)) {
    sd_close(file);
    return;
  }

  for (;;) {
    unsigned read_bytes = 0;
    if (sd_read(file, s_io_buffer, sizeof(s_io_buffer), &read_bytes) != SD_OK) {
      break;
    }
    if (read_bytes == 0) {
      break;
    }
    if (!webhttp::SendAll(socket, s_io_buffer, read_bytes)) {
      break;
    }
    CScheduler::Get()->Yield();
  }
  sd_close(file);
}

void WebUiFsUpload(CSocket *socket, const char *query,
                   const unsigned char *prefetched, unsigned prefetched_len,
                   long content_length) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }
  if (content_length < 0) {
    webhttp::SendText(socket, 411, "Length Required",
                      "Content-Length header required\n");
    return;
  }
  if (IsProtectedPath(clean)) {
    webhttp::SendText(socket, 403, "Forbidden", "that path is protected\n");
    return;
  }

  char overwrite[4];
  boolean allow_overwrite =
      webhttp::QueryParam(query, "overwrite", overwrite, sizeof(overwrite)) &&
      overwrite[0] == '1';

  sd_info existing;
  if (sd_stat_info(V(clean), &existing) == SD_OK) {
    if (existing.is_dir) {
      webhttp::SendText(socket, 409, "Conflict", "target is a directory\n");
      return;
    }
    if (!allow_overwrite) {
      webhttp::SendText(socket, 409, "Conflict", "file exists\n");
      return;
    }
  }

  // Write to "<path>.part" and swap it into place on success, so an
  // aborted upload never leaves a truncated file.
  char temppath[576];
  if ((unsigned) snprintf(temppath, sizeof(temppath), "%s.part", clean) >=
      sizeof(temppath)) {
    webhttp::SendText(socket, 400, "Bad Request", "path too long\n");
    return;
  }

  unsigned long total = (unsigned long) content_length;
  BodyResult body_result = ReceiveBodyToFile(
      socket, temppath, prefetched, prefetched_len, total, FALSE);
  if (body_result != BODY_OK) {
    SendBodyFailure(socket, body_result);
    return;
  }

  sd_unlink(V(clean));  // ignore result: file may not exist
  if (sd_rename(V(temppath), V(clean)) != SD_OK) {
    sd_unlink(V(temppath));
    webhttp::SendText(socket, 500, "Internal Server Error",
                      "cannot finalise upload\n");
    return;
  }

  // Keep the file's original modified time when the client supplies it;
  // otherwise (or if it can't be applied) the file keeps the upload time.
  char mtime[24];
  sd_time stamp;
  if (webhttp::QueryParam(query, "mtime", mtime, sizeof(mtime)) &&
      ParseDateTime(mtime, &stamp)) {
    sd_set_mtime(V(clean), &stamp);
  }

  char body[64];
  int bn = snprintf(body, sizeof(body), "{\"ok\":true,\"size\":%lu}", total);
  webhttp::SendResponse(socket, 200, "OK", "application/json", body,
                        bn > 0 ? (unsigned) bn : 0);
}

void WebUiFsSave(CSocket *socket, const char *query,
                 const unsigned char *prefetched, unsigned prefetched_len,
                 long content_length) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }
  if (!IsEditablePath(clean)) {
    webhttp::SendText(socket, 403, "Forbidden", "that file cannot be edited\n");
    return;
  }
  if (content_length < 0) {
    webhttp::SendText(socket, 411, "Length Required",
                      "Content-Length header required\n");
    return;
  }
  if ((unsigned long) content_length > WEBUI_FS_EDIT_MAX) {
    webhttp::SendText(socket, 413, "Payload Too Large",
                      "file too large to edit\n");
    return;
  }

  sd_info existing;
  boolean exists = sd_stat_info(V(clean), &existing) == SD_OK;
  if (exists && existing.is_dir) {
    webhttp::SendText(socket, 409, "Conflict", "target is a directory\n");
    return;
  }

  // A keymap can be in a nested folder, so size these for the longest
  // resolved path plus the ".part" / ".bak" suffix.
  char temppath[sizeof(clean) + 8];
  char bakpath[sizeof(clean) + 8];
  if ((unsigned) snprintf(temppath, sizeof(temppath), "%s.part", clean) >=
          sizeof(temppath) ||
      (unsigned) snprintf(bakpath, sizeof(bakpath), "%s.bak", clean) >=
          sizeof(bakpath)) {
    webhttp::SendText(socket, 400, "Bad Request", "path too long\n");
    return;
  }

  unsigned long total = (unsigned long) content_length;
  BodyResult body_result = ReceiveBodyToFile(
      socket, temppath, prefetched, prefetched_len, total, TRUE);
  if (body_result != BODY_OK) {
    SendBodyFailure(socket, body_result);
    return;
  }

  // Keep the previous version as "<name>.bak". The original is only moved
  // aside once the new one is fully written, and is put back if the final
  // rename fails, so a failed save never leaves a config file missing.
  if (exists) {
    sd_unlink(V(bakpath));  // ignore result: an older backup may not exist
    if (sd_rename(V(clean), V(bakpath)) != SD_OK) {
      sd_unlink(V(temppath));
      webhttp::SendText(socket, 500, "Internal Server Error",
                        "cannot back up the existing file\n");
      return;
    }
  }
  if (sd_rename(V(temppath), V(clean)) != SD_OK) {
    if (exists) {
      sd_rename(V(bakpath), V(clean));
    }
    sd_unlink(V(temppath));
    webhttp::SendText(socket, 500, "Internal Server Error",
                      "cannot finalise save\n");
    return;
  }

  char body[64];
  int bn = snprintf(body, sizeof(body), "{\"ok\":true,\"size\":%lu}", total);
  webhttp::SendResponse(socket, 200, "OK", "application/json", body,
                        bn > 0 ? (unsigned) bn : 0);
}

void WebUiFsDelete(CSocket *socket, const char *query) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }
  if (IsProtectedPath(clean)) {
    webhttp::SendText(socket, 403, "Forbidden", "that path is protected\n");
    return;
  }

  // &recursive=1 deletes a folder together with its contents; without it
  // only an empty folder can be removed.
  char recursive[4];
  sd_info stat;
  if (webhttp::QueryParam(query, "recursive", recursive, sizeof(recursive)) &&
      recursive[0] == '1' && sd_stat_info(V(clean), &stat) == SD_OK &&
      stat.is_dir) {
    unsigned removed = 0;
    if (sd_remove_tree(V(clean), &removed, YieldWhileDeleting, 0) == SD_OK) {
      const char *ok = "{\"ok\":true}";
      webhttp::SendResponse(socket, 200, "OK", "application/json", ok,
                            (unsigned) strlen(ok));
      return;
    }
    char text[128];
    snprintf(text, sizeof(text),
             "could not delete everything (a file may be read-only): %u "
             "item%s removed, the rest of the folder is still there\n",
             removed, removed == 1 ? "" : "s");
    webhttp::SendText(socket, 409, "Conflict", text);
    return;
  }

  int rc = sd_unlink(V(clean));
  if (rc == SD_NOT_FOUND || rc == SD_INVALID) {
    webhttp::SendText(socket, 404, "Not Found", "no such file\n");
    return;
  }
  if (rc == SD_DENIED) {
    webhttp::SendText(socket, 409, "Conflict",
                      "cannot delete (directory not empty or read-only)\n");
    return;
  }
  if (rc != SD_OK) {
    webhttp::SendText(socket, 500, "Internal Server Error", "delete failed\n");
    return;
  }

  const char *ok = "{\"ok\":true}";
  webhttp::SendResponse(socket, 200, "OK", "application/json", ok,
                        (unsigned) strlen(ok));
}

// ---- new folder / rename ----

void WebUiFsMkdir(CSocket *socket, const char *query) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }
  if (!IsValidEntryName(strrchr(clean, '/') + 1)) {
    webhttp::SendText(socket, 400, "Bad Request", kBadNameText);
    return;
  }
  if (IsProtectedPath(clean)) {
    webhttp::SendText(socket, 403, "Forbidden", "that path is protected\n");
    return;
  }

  if (sd_stat_info(V(clean), 0) == SD_OK) {
    webhttp::SendText(socket, 409, "Conflict", "already exists\n");
    return;
  }

  int rc = sd_mkdir(V(clean));
  if (rc == SD_EXISTS) {
    webhttp::SendText(socket, 409, "Conflict", "already exists\n");
    return;
  }
  if (rc == SD_NOT_FOUND) {
    webhttp::SendText(socket, 404, "Not Found", "parent folder not found\n");
    return;
  }
  if (rc != SD_OK) {
    webhttp::SendText(socket, 500, "Internal Server Error",
                      "cannot create folder\n");
    return;
  }

  const char *ok = "{\"ok\":true}";
  webhttp::SendResponse(socket, 200, "OK", "application/json", ok,
                        (unsigned) strlen(ok));
}

// POST /api/fs/rename?path=/dir/old&to=new: renames within the same folder,
// so `to` is a bare name, never a path.
void WebUiFsRename(CSocket *socket, const char *query) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }

  char new_name[256];
  if (!webhttp::QueryParam(query, "to", new_name, sizeof(new_name)) ||
      !IsValidEntryName(new_name)) {
    webhttp::SendText(socket, 400, "Bad Request", kBadNameText);
    return;
  }

  const char *old_name = strrchr(clean, '/') + 1;
  char new_clean[560];
  if ((unsigned) snprintf(new_clean, sizeof(new_clean), "%.*s/%s",
                          (int) (old_name - 1 - clean), clean,
                          new_name) >= sizeof(new_clean)) {
    webhttp::SendText(socket, 400, "Bad Request", "path too long\n");
    return;
  }

  // Neither the source nor the new name may be a protected one (that would
  // let a rename create or replace settings.txt, or touch /firmware).
  if (IsProtectedPath(clean) || IsProtectedPath(new_clean)) {
    webhttp::SendText(socket, 403, "Forbidden", "that path is protected\n");
    return;
  }

  if (sd_stat_info(V(clean), 0) != SD_OK) {
    webhttp::SendText(socket, 404, "Not Found", "no such file\n");
    return;
  }

  boolean unchanged = strcmp(old_name, new_name) == 0;
  boolean case_only = !unchanged && CiEqual(old_name, new_name);
  if (!unchanged && !case_only && sd_stat_info(V(new_clean), 0) == SD_OK) {
    webhttp::SendText(socket, 409, "Conflict", "already exists\n");
    return;
  }

  int rc = unchanged ? SD_OK : sd_rename(V(clean), V(new_clean));
  if (rc == SD_EXISTS) {
    webhttp::SendText(socket, 409, "Conflict", "already exists\n");
    return;
  }
  if (rc == SD_NOT_FOUND) {
    webhttp::SendText(socket, 404, "Not Found", "no such file\n");
    return;
  }
  if (rc != SD_OK) {
    webhttp::SendText(socket, 500, "Internal Server Error",
                      "rename failed\n");
    return;
  }

  const char *ok = "{\"ok\":true}";
  webhttp::SendResponse(socket, 200, "OK", "application/json", ok,
                        (unsigned) strlen(ok));
}

// ---- autostart ----

void WebUiFsAutostart(CSocket *socket, const char *query) {
  char clean[560];
  if (ResolveTarget(socket, query, clean, sizeof(clean), TRUE) != 0) {
    return;
  }
  if (!IsAutostartable(clean)) {
    webhttp::SendText(socket, 400, "Bad Request",
                      "not an autostartable file type\n");
    return;
  }

  sd_info info;
  if (sd_stat_info(V(clean), &info) != SD_OK || info.is_dir) {
    webhttp::SendText(socket, 404, "Not Found", "no such file\n");
    return;
  }

  // Runs later on the emulator core, through stdio, so it gets the path
  // with its volume ("SD:/disks/x.dsk", "SD2:/hd/big.hdf").
  char volume_path[600];
  if ((unsigned) snprintf(volume_path, sizeof(volume_path), "%s:%s",
                          VolumeName(), clean) >=
      sizeof(volume_path)) {
    webhttp::SendText(socket, 400, "Bad Request", "path too long\n");
    return;
  }
  // Failures are only logged there.
  emu_autostart_interrupt(volume_path);

  const char *ok = "{\"ok\":true}";
  webhttp::SendResponse(socket, 202, "Accepted", "application/json", ok,
                        (unsigned) strlen(ok));
}

#endif  // BMC64_WEBUI
