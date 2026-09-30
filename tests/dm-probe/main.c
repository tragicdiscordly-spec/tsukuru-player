/* Probes how much "direct memory" (the memory the PS5 video output needs) a payload may allocate.
 * Tries a range of sizes, releases each block right away, and reports the result in one notification.
 */
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);
int sceKernelAllocateMainDirectMemory(size_t len, size_t align, int type, off_t* phys_out);
int sceKernelReleaseDirectMemory(off_t start, size_t len);
size_t sceKernelGetDirectMemorySize(void);
int sceKernelAvailableDirectMemorySize(off_t start, off_t end, size_t align, off_t* phys_out, size_t* size_out);

static void notify(const char* fmt, ...) {
  notify_request_t req;
  va_list ap;

  memset(&req, 0, sizeof req);
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

int main(void) {
  char line[512];
  size_t n = 0;
  size_t total = sceKernelGetDirectMemorySize();
  off_t phys = 0;
  size_t avail = 0;

  int ar = sceKernelAvailableDirectMemorySize(0, (off_t)total, 0x20000, &phys, &avail);
  notify("DM probe: total %zu MB, available call=%d avail=%zu MB (errno %d)", total >> 20, ar, avail >> 20, errno);

  static const size_t mb[] = {64, 32, 24, 16, 8, 4, 2, 1};
  for (size_t i = 0; i < sizeof mb / sizeof mb[0]; i++) {
    off_t p = 0;
    errno = 0;
    int r = sceKernelAllocateMainDirectMemory(mb[i] << 20, 0x20000, 3, &p);
    int e = errno;
    n += snprintf(line + n, sizeof line - n, "%zuM:%s ", mb[i], r == 0 ? "ok" : strerror(e));
    if (r == 0) {
      sceKernelReleaseDirectMemory(p, mb[i] << 20);
    }
  }
  notify("DM probe: %s", line);
  return 0;
}
