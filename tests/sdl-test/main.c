/* Minimal SDL2 smoke test for the PS5: video, controller and audio.
 *
 * Shows a colour-cycling screen with a square you can move using the left stick or D-pad. Sixteen small
 * boxes along the bottom light up for each pressed controller button. Progress is reported with PS5
 * notifications so problems are visible without a debugger. Exits by itself after RUN_SECONDS.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

#define RUN_SECONDS 30

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

static void notify(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#include <stdarg.h>
static void notify(const char* fmt, ...) {
  notify_request_t req;
  va_list ap;

  memset(&req, 0, sizeof req);
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
}

static double beep_phase;

static void audio_callback(void* userdata, Uint8* stream, int len) {
  Sint16* out = (Sint16*)stream;
  int samples = len / (int)sizeof(Sint16) / 2;
  int* remaining = (int*)userdata;

  for (int i = 0; i < samples; i++) {
    Sint16 v = 0;
    if (*remaining > 0) {
      v = (Sint16)(sin(beep_phase) * 3000); /* quiet */
      beep_phase += 2.0 * M_PI * 440.0 / 48000.0;
      (*remaining)--;
    }
    out[i * 2] = v;
    out[i * 2 + 1] = v;
  }
}

int main(void) {
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO) != 0) {
    notify("SDL test: SDL_Init failed: %s", SDL_GetError());
    return 1;
  }
  notify("SDL test: SDL_Init ok (%d joysticks)", SDL_NumJoysticks());

  SDL_Window* win = SDL_CreateWindow("sdl-test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920, 1080,
                                     SDL_WINDOW_FULLSCREEN);
  if (!win) {
    notify("SDL test: CreateWindow failed: %s", SDL_GetError());
    SDL_Quit();
    return 1;
  }

  SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  const char* kind = "accelerated";
  if (!ren) {
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    kind = "software";
  }
  if (!ren) {
    notify("SDL test: CreateRenderer failed: %s", SDL_GetError());
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 1;
  }
  SDL_RendererInfo info;
  SDL_GetRendererInfo(ren, &info);
  int w = 0, h = 0;
  SDL_GetRendererOutputSize(ren, &w, &h);
  notify("SDL test: %s renderer '%s', %dx%d", kind, info.name, w, h);

  SDL_Joystick* joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : NULL;

  int beep_left = 48000; /* one second */
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = 48000;
  want.format = AUDIO_S16SYS;
  want.channels = 2;
  want.samples = 1024;
  want.callback = audio_callback;
  want.userdata = &beep_left;
  SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (audio) {
    SDL_PauseAudioDevice(audio, 0);
    notify("SDL test: audio open (%d Hz)", have.freq);
  } else {
    notify("SDL test: audio failed: %s", SDL_GetError());
  }

  double x = w / 2.0, y = h / 2.0;
  Uint32 start = SDL_GetTicks(), last = start, frames = 0, fps_time = start;

  while (SDL_GetTicks() - start < RUN_SECONDS * 1000u) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      if (e.type == SDL_QUIT) {
        goto done;
      }
    }

    Uint32 now = SDL_GetTicks();
    double dt = (now - last) / 1000.0;
    last = now;

    if (joy) {
      SDL_JoystickUpdate();
      double ax = SDL_JoystickGetAxis(joy, 0) / 32768.0, ay = SDL_JoystickGetAxis(joy, 1) / 32768.0;
      if (fabs(ax) < 0.15) ax = 0;
      if (fabs(ay) < 0.15) ay = 0;
      Uint8 hat = SDL_JoystickNumHats(joy) > 0 ? SDL_JoystickGetHat(joy, 0) : 0;
      if (hat & SDL_HAT_LEFT) ax = -1;
      if (hat & SDL_HAT_RIGHT) ax = 1;
      if (hat & SDL_HAT_UP) ay = -1;
      if (hat & SDL_HAT_DOWN) ay = 1;
      x += ax * 600 * dt;
      y += ay * 600 * dt;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x > w - 100) x = w - 100;
    if (y > h - 100) y = h - 100;

    double t = (now - start) / 1000.0;
    SDL_SetRenderDrawColor(ren, (Uint8)(127 + 127 * sin(t)), (Uint8)(127 + 127 * sin(t + 2.1)),
                           (Uint8)(127 + 127 * sin(t + 4.2)), 255);
    SDL_RenderClear(ren);

    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_Rect r = {(int)x, (int)y, 100, 100};
    SDL_RenderFillRect(ren, &r);

    for (int b = 0; b < 16; b++) {
      SDL_Rect box = {40 + b * 60, h - 80, 50, 50};
      int down = joy && b < SDL_JoystickNumButtons(joy) && SDL_JoystickGetButton(joy, b);
      SDL_SetRenderDrawColor(ren, down ? 255 : 40, down ? 255 : 40, down ? 0 : 40, 255);
      SDL_RenderFillRect(ren, &box);
    }

    SDL_RenderPresent(ren);

    frames++;
    if (now - fps_time >= 10000) {
      notify("SDL test: %u fps", (unsigned)(frames * 1000 / (now - fps_time)));
      frames = 0;
      fps_time = now;
    }
  }

done:
  notify("SDL test: finished");
  if (audio) SDL_CloseAudioDevice(audio);
  if (joy) SDL_JoystickClose(joy);
  SDL_DestroyRenderer(ren);
  SDL_DestroyWindow(win);
  SDL_Quit();
  return 0;
}
