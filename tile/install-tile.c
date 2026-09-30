/* Installs (or removes) an "Tsukuru Player" tile on the PS5 home screen.
 *
 * A home screen app is a folder in /user/app/<title id> with sce_sys/param.json and icon0.png; the
 * param.json's deeplinkUri is opened when the tile is selected. Ours opens a small page served by
 * websrv (which has to be running) that asks websrv to start the Player as a foreground app.
 *
 * Based on install-ps5.c from ftpsrv (https://github.com/ps5-payload-dev/ftpsrv), which is
 * licensed under the GPL, either version 3 or (at your option) any later version.
 *
 * Build with -DUNINSTALL to get the payload that removes the tile again.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <sys/stat.h>

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

INCASSET(param, "assets/param.json");
INCASSET(icon0, "assets/icon0.png");
INCASSET(launch, "assets/launch.html");


typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;


static void
notify(const char* msg) {
  notify_request_t req = {0};

  snprintf(req.message, sizeof(req.message), "%s", msg);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}


#ifndef UNINSTALL
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
#endif


int
main(int argc, char *argv[]) {
  int err;

  if((err=sceAppInstUtilInitialize())) {
    printf("sceAppInstUtilInitialize: error 0x%08X\n", err);
    notify("Tsukuru Player tile: sceAppInstUtilInitialize failed");
    return -1;
  }

  sceAppInstUtilAppUnInstall(TITLE_ID);

#ifdef UNINSTALL
  notify("Tsukuru Player tile removed");
  return 0;
#else
  mkdir("/user/app/"TITLE_ID, 0755);
  mkdir("/user/app/"TITLE_ID"/sce_sys", 0755);

  if(install_file("/user/app/"TITLE_ID"/launch.html", launch, launch_size) ||
     install_file("/user/app/"TITLE_ID"/sce_sys/icon0.png", icon0, icon0_size) ||
     install_file("/user/app/"TITLE_ID"/sce_sys/param.json", param, param_size)) {
    perror("install_file");
    notify("Tsukuru Player tile: could not write files");
    return -1;
  }

  if((err=install_app(TITLE_ID, "/user/app/"))) {
    printf("install_app: error 0x%08X\n", err);
    notify("Tsukuru Player tile: install failed");
    return -1;
  }

  notify("Tsukuru Player tile installed");
  return 0;
#endif
}
