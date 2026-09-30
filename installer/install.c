/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* Installs Tsukuru Player for the PS5 from a USB stick, without a PC.
 *
 * Put the "tsukuru-player" folder (made by scripts/make-usb-bundle.sh) on a USB stick, start websrv on the console
 * and open install.html from the stick in the console's web browser. That page starts this program, which
 *
 *   - copies the folders in <bundle>/files/homebrew to /data/homebrew
 *   - copies the files in <bundle>/files/lib to /user/homebrew/lib
 *   - installs the "Tsukuru Player" tile on the home screen
 *
 * and shows its progress as notifications. Files that are already there unchanged are skipped, so running it again
 * is an update. It only writes to the three places above. It is given the bundle folder as its first argument
 * (install.html does that); without one it looks for tsukuru-player/files on the USB sticks.
 *
 * For testing, INSTALL_DST_ROOT=<dir> puts everything below <dir> instead (and leaves the tile alone).
 *
 * The tile part is based on install-ps5.c from ftpsrv (https://github.com/ps5-payload-dev/ftpsrv), which is
 * licensed under the GPL, either version 3 or (at your option) any later version.
 */
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <ps5/kernel.h>

#define TITLE_ID "EPRG00001"

#define INCASSET(name, file)			\
  __asm__(".section .rodata\n"			\
	  ".global " #name "\n"			\
	  ".global " #name "_end\n"		\
	  ".global " #name "_size\n"		\
	  ".align 16\n"				\
	  #name ":\n"				\
	  ".incbin \"" file "\"\n"		\
	  #name "_end:\n"			\
	  #name "_size:\n"			\
	  ".quad " #name "_end - " #name "\n"	\
	  ".previous\n");			\
  extern const uint8_t name[];			\
  extern const size_t name##_size;


int sceAppInstUtilInitialize(void);
int sceAppInstUtilAppInstallAll(void*);
int sceAppInstUtilAppUnInstall(const char*);
int sceKernelSendNotificationRequest(int, void*, size_t, int);

INCASSET(param, "../tile/assets/param.json");
INCASSET(icon0, "../tile/assets/icon0.png");
INCASSET(launch, "../tile/assets/launch.html");


typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;


static void
notify(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>

static void
notify(const char* fmt, ...) {
  notify_request_t req = {0};
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(req.message, sizeof(req.message), fmt, ap);
  va_end(ap);
  printf("%s\n", req.message);
  fflush(stdout);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


/* The console's address on the local network, "" if there is none. */
static void
find_local_ip(char* out, size_t size) {
  struct ifaddrs* list = NULL;

  out[0] = '\0';
  if(getifaddrs(&list)) {
    return;
  }
  for(struct ifaddrs* a=list; a; a=a->ifa_next) {
    if(!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) {
      continue;
    }
    const unsigned char* b = (const unsigned char*)&((struct sockaddr_in*)a->ifa_addr)->sin_addr;
    if(b[0] == 127 || (b[0] == 169 && b[1] == 254)) {
      continue;
    }
    snprintf(out, size, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    break;
  }
  freeifaddrs(list);
}


/* ------------------------------------------------------------------------------------------ copying */

static const char* dst_root = "";      /* INSTALL_DST_ROOT, for tests */
static uint64_t bytes_copied, files_copied, files_skipped;

static int
make_dirs(const char* path) {
  char tmp[1024];
  size_t len = snprintf(tmp, sizeof(tmp), "%s", path);

  if(len >= sizeof(tmp)) {
    return -1;
  }
  for(size_t i=1; i<len; i++) {
    if(tmp[i] == '/') {
      tmp[i] = '\0';
      mkdir(tmp, 0755);
      tmp[i] = '/';
    }
  }
  if(mkdir(tmp, 0755) && errno != EEXIST) {
    return -1;
  }
  return 0;
}


/* A name that is safe to join to a destination folder. */
static int
safe_name(const char* name) {
  return name[0] && strcmp(name, ".") && strcmp(name, "..") && !strchr(name, '/');
}


static int
copy_file(const char* src, const char* dst, const struct stat* st) {
  struct stat old;
  struct timeval times[2];
  static char buf[1 << 20];
  char tmp[1100];
  FILE *in, *out;
  size_t n;

  /* unchanged: same size and modification time (the time of the copy is set to the time of the source) */
  if(!stat(dst, &old) && old.st_size == st->st_size && old.st_mtime == st->st_mtime) {
    files_skipped++;
    return 0;
  }

  if(!(in=fopen(src, "rb"))) {
    printf("cannot read %s: %s\n", src, strerror(errno));
    return -1;
  }
  /* Write to a temporary name and rename it over the old file. A program that is still running (or waiting in the
   * background) has the old file mapped; overwriting it in place would crash that program. */
  snprintf(tmp, sizeof(tmp), "%s.new", dst);
  if(!(out=fopen(tmp, "wb"))) {
    printf("cannot write %s: %s\n", dst, strerror(errno));
    fclose(in);
    return -1;
  }
  while((n=fread(buf, 1, sizeof(buf), in)) > 0) {
    if(fwrite(buf, 1, n, out) != n) {
      printf("cannot write %s: %s\n", dst, strerror(errno));
      fclose(in);
      fclose(out);
      unlink(tmp);
      return -1;
    }
    bytes_copied += n;
  }
  fclose(in);
  if(fclose(out)) {
    printf("cannot write %s: %s\n", dst, strerror(errno));
    unlink(tmp);
    return -1;
  }

  times[0].tv_sec = times[1].tv_sec = st->st_mtime;
  times[0].tv_usec = times[1].tv_usec = 0;
  utimes(tmp, times);
  chmod(tmp, 0755);
  if(rename(tmp, dst)) {
    printf("cannot replace %s: %s\n", dst, strerror(errno));
    unlink(tmp);
    return -1;
  }
  files_copied++;
  return 0;
}


static int
copy_tree(const char* src, const char* dst) {
  DIR* d;
  struct dirent* e;
  int errors = 0;

  if(make_dirs(dst)) {
    printf("cannot create %s: %s\n", dst, strerror(errno));
    return -1;
  }
  if(!(d=opendir(src))) {
    printf("cannot open %s: %s\n", src, strerror(errno));
    return -1;
  }
  while((e=readdir(d))) {
    char s[1024], t[1024];
    struct stat st;

    if(!safe_name(e->d_name)) {
      continue;
    }
    if(snprintf(s, sizeof(s), "%s/%s", src, e->d_name) >= (int)sizeof(s) ||
       snprintf(t, sizeof(t), "%s/%s", dst, e->d_name) >= (int)sizeof(t) || stat(s, &st)) {
      errors++;
      continue;
    }
    if(S_ISDIR(st.st_mode)) {
      errors += copy_tree(s, t) ? 1 : 0;
    } else if(S_ISREG(st.st_mode)) {
      errors += copy_file(s, t, &st) ? 1 : 0;
    }
  }
  closedir(d);
  return errors ? -1 : 0;
}


/* ------------------------------------------------------------------------------------------ the tile */

static int
install_file(const char* path, const uint8_t* data, size_t size) {
  FILE* f;

  if(!(f=fopen(path, "w"))) {
    return -1;
  }
  if(fwrite(data, size, 1, f) != 1) {
    fclose(f);
    return -1;
  }
  fclose(f);
  return 0;
}


static int
install_app(const char* title_id, const char* dir) {
  int (*sceAppInstUtilAppInstallTitleDir)(const char*, const char*, void*) = 0;
  const char* nid = "Wudg3Xe3heE";
  uint32_t handle;

  if(!kernel_dynlib_handle(-1, "libSceAppInstUtil.sprx", &handle)) {
    sceAppInstUtilAppInstallTitleDir = (void*)kernel_dynlib_resolve(-1, handle, nid);
  }

  if(sceAppInstUtilAppInstallTitleDir) {
    return sceAppInstUtilAppInstallTitleDir(title_id, dir, 0);
  }

  return sceAppInstUtilAppInstallAll(0);
}


static int
install_tile(void) {
  int err;

  if((err=sceAppInstUtilInitialize())) {
    printf("sceAppInstUtilInitialize: error 0x%08X\n", err);
    return -1;
  }

  sceAppInstUtilAppUnInstall(TITLE_ID);
  mkdir("/user/app/"TITLE_ID, 0755);
  mkdir("/user/app/"TITLE_ID"/sce_sys", 0755);

  if(install_file("/user/app/"TITLE_ID"/launch.html", launch, launch_size) ||
     install_file("/user/app/"TITLE_ID"/sce_sys/icon0.png", icon0, icon0_size) ||
     install_file("/user/app/"TITLE_ID"/sce_sys/param.json", param, param_size)) {
    perror("install_file");
    return -1;
  }

  if((err=install_app(TITLE_ID, "/user/app/"))) {
    printf("install_app: error 0x%08X\n", err);
    return -1;
  }
  return 0;
}


/* ------------------------------------------------------------------------------------------ main */

static int
is_dir(const char* path) {
  struct stat st;
  return !stat(path, &st) && S_ISDIR(st.st_mode);
}


int
main(int argc, char *argv[]) {
  char bundle[512] = "", src[600], dst[600];
  const char* env_root = getenv("INSTALL_DST_ROOT");
  int failed = 0;

  if(env_root && *env_root) {
    dst_root = env_root;
  }

  if(argc > 1 && argv[1][0] == '/') {
    snprintf(bundle, sizeof(bundle), "%s", argv[1]);
  } else {
    for(int i=0; i<8 && !bundle[0]; i++) {
      char candidate[64];
      snprintf(candidate, sizeof(candidate), "/mnt/usb%d/tsukuru-player", i);
      if(is_dir(candidate)) {
        snprintf(bundle, sizeof(bundle), "%s", candidate);
      }
    }
  }

  snprintf(src, sizeof(src), "%s/files", bundle);
  if(!bundle[0] || !is_dir(src)) {
    notify("Tsukuru Player installer: the folder tsukuru-player was not found on the USB stick");
    return 1;
  }

  notify("Tsukuru Player installer: installing from %s ...", bundle);

  static const struct { const char* from; const char* to; } parts[] = {
    { "homebrew", "/data/homebrew" },
    { "lib",      "/user/homebrew/lib" },
  };
  for(size_t i=0; i<sizeof(parts)/sizeof(parts[0]); i++) {
    char from[700];

    snprintf(from, sizeof(from), "%s/%s", src, parts[i].from);
    if(!is_dir(from)) {
      continue;
    }
    snprintf(dst, sizeof(dst), "%s%s", dst_root, parts[i].to);

    /* one folder of the bundle at a time, so that the notifications show how far it is */
    DIR* d = opendir(from);
    struct dirent* e;
    if(!d) {
      failed++;
      continue;
    }
    while((e=readdir(d))) {
      char sub_from[1100], sub_to[1100];
      struct stat st;

      if(!safe_name(e->d_name)) {
	continue;
      }
      snprintf(sub_from, sizeof(sub_from), "%s/%s", from, e->d_name);
      snprintf(sub_to, sizeof(sub_to), "%s/%s", dst, e->d_name);
      if(stat(sub_from, &st)) {
	continue;
      }
      if(S_ISDIR(st.st_mode)) {
	notify("Tsukuru Player installer: copying %s ...", e->d_name);
	if(copy_tree(sub_from, sub_to)) {
	  failed++;
	}
      } else if(S_ISREG(st.st_mode)) {
	make_dirs(dst);
	if(copy_file(sub_from, sub_to, &st)) {
	  failed++;
	}
      }
    }
    closedir(d);
  }

  if(failed) {
    notify("Tsukuru Player installer: %d parts could not be copied (USB stick full or removed?)", failed);
    return 1;
  }

  if(dst_root[0]) {
    notify("Tsukuru Player installer (test): copied %llu MB, %llu files unchanged", (unsigned long long)(bytes_copied >> 20),
	   (unsigned long long)files_skipped);
    return 0;
  }

  if(install_tile()) {
    notify("Tsukuru Player installer: the files are copied, but the home screen tile could not be installed");
    return 1;
  }

  notify("Tsukuru Player installed (%llu MB copied, %llu files were up to date). Open the Tsukuru Player tile on the home screen.",
	 (unsigned long long)(bytes_copied >> 20), (unsigned long long)files_skipped);

  char ip[64];
  find_local_ip(ip, sizeof(ip));
  if(ip[0]) {
    notify("Add games from a phone or PC: start ftpsrv, then connect an FTP app to %s port 2121. Or put them on a USB stick in a folder called games.", ip);
  }
  return 0;
}
