/* OpenGL (software, Mesa OSMesa) frame rate test for the PS5: the go/no-go check for running mkxp-z.
 *
 * Renders roughly what an RPG Maker XP/VX/Ace game does per frame: 3 tile layers of 20x15 tiles
 * plus 100 moving sprites (alpha blended, textured) into a 640x480 framebuffer object, then draws
 * that texture scaled up to the screen with a second shader. Reports GL/GLSL versions and the frame
 * rate through PS5 notifications, and exits after RUN_SECONDS.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>
#include <unistd.h>

#include <SDL.h>

#define RUN_SECONDS 25
#define GAME_W 640
#define GAME_H 480

/* --- minimal GL declarations (we load everything through SDL_GL_GetProcAddress) --- */
typedef unsigned int GLenum, GLuint, GLbitfield;
typedef int GLint, GLsizei;
typedef unsigned char GLboolean, GLubyte;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_TRIANGLES 4
#define GL_FLOAT 0x1406
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_TEXTURE0 0x84C0

#define GLFUNCS(X) \
	X(const GLubyte*, glGetString, (GLenum)) \
	X(void, glClear, (GLbitfield)) \
	X(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) \
	X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
	X(void, glEnable, (GLenum)) \
	X(void, glDisable, (GLenum)) \
	X(void, glBlendFunc, (GLenum, GLenum)) \
	X(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
	X(void, glGenTextures, (GLsizei, GLuint*)) \
	X(void, glBindTexture, (GLenum, GLuint)) \
	X(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
	X(void, glTexParameteri, (GLenum, GLenum, GLint)) \
	X(void, glFinish, (void)) \
	X(GLuint, glCreateShader, (GLenum)) \
	X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
	X(void, glCompileShader, (GLuint)) \
	X(void, glGetShaderiv, (GLuint, GLenum, GLint*)) \
	X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
	X(GLuint, glCreateProgram, (void)) \
	X(void, glAttachShader, (GLuint, GLuint)) \
	X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*)) \
	X(void, glLinkProgram, (GLuint)) \
	X(void, glGetProgramiv, (GLuint, GLenum, GLint*)) \
	X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
	X(void, glUseProgram, (GLuint)) \
	X(GLint, glGetUniformLocation, (GLuint, const GLchar*)) \
	X(void, glUniform1i, (GLint, GLint)) \
	X(void, glGenBuffers, (GLsizei, GLuint*)) \
	X(void, glBindBuffer, (GLenum, GLuint)) \
	X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
	X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
	X(void, glEnableVertexAttribArray, (GLuint)) \
	X(void, glActiveTexture, (GLenum)) \
	X(void, glGenFramebuffers, (GLsizei, GLuint*)) \
	X(void, glBindFramebuffer, (GLenum, GLuint)) \
	X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
	X(GLenum, glCheckFramebufferStatus, (GLenum))

#define DECLARE(ret, name, args) static ret (*name) args;
GLFUNCS(DECLARE)
#define LOAD(ret, name, args) name = (ret (*) args)SDL_GL_GetProcAddress(#name);
static void load_gl(void) { GLFUNCS(LOAD) }

/* --- PS5 notifications --- */
typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t*, size_t, int);

static void notify(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void notify(const char* fmt, ...) {
  notify_request_t req;
  va_list ap;
  memset(&req, 0, sizeof req);
  va_start(ap, fmt);
  vsnprintf(req.message, sizeof req.message, fmt, ap);
  va_end(ap);
  sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
  printf("%s\n", req.message);
  fflush(stdout);
}

#define LOG(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

/* Where did it die? There is no debugger, so report the signal and fault address. */
static void crash_handler(int sig, siginfo_t* info, void* ctx) {
	(void)ctx;
	char buf[128];
	int n = snprintf(buf, sizeof buf, "\n*** CRASH signal=%d code=%d fault_addr=%p\n", sig, info->si_code, info->si_addr);
	write(2, buf, n);
	_exit(128 + sig);
}

/* --- shaders (GLSL 1.20 like mkxp's original shaders; works in compatibility contexts) --- */
static const char* SPRITE_VS =
	"#version 120\n"
	"attribute vec2 pos; attribute vec2 uv; attribute vec4 color;\n"
	"varying vec2 v_uv; varying vec4 v_color;\n"
	"void main() { v_uv = uv; v_color = color;\n"
	"  gl_Position = vec4(pos.x / 320.0 - 1.0, 1.0 - pos.y / 240.0, 0.0, 1.0); }\n";
static const char* SPRITE_FS =
	"#version 120\n"
	"uniform sampler2D tex; varying vec2 v_uv; varying vec4 v_color;\n"
	"void main() { vec4 c = texture2D(tex, v_uv) * v_color;\n"
	"  c.rgb = mix(c.rgb, vec3(dot(c.rgb, vec3(0.3, 0.59, 0.11))), 0.1); gl_FragColor = c; }\n";
static const char* BLIT_VS =
	"#version 120\n"
	"attribute vec2 pos; attribute vec2 uv; attribute vec4 color;\n"
	"varying vec2 v_uv;\n"
	"void main() { v_uv = uv; gl_Position = vec4(pos, 0.0, 1.0); }\n";
static const char* BLIT_FS =
	"#version 120\n"
	"uniform sampler2D tex; varying vec2 v_uv;\n"
	"void main() { gl_FragColor = texture2D(tex, v_uv); }\n";

static GLuint make_program(const char* vs, const char* fs, const char* what) {
	GLuint prog = glCreateProgram();
	const char* srcs[2] = {vs, fs};
	GLenum types[2] = {GL_VERTEX_SHADER, GL_FRAGMENT_SHADER};
	for (int i = 0; i < 2; i++) {
		GLuint s = glCreateShader(types[i]);
		glShaderSource(s, 1, &srcs[i], NULL);
		glCompileShader(s);
		GLint ok = 0;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) {
			char log[512] = {0};
			glGetShaderInfoLog(s, sizeof log - 1, NULL, log);
			notify("GL test: %s shader %d failed: %s", what, i, log);
			return 0;
		}
		glAttachShader(prog, s);
	}
	glBindAttribLocation(prog, 0, "pos");
	glBindAttribLocation(prog, 1, "uv");
	glBindAttribLocation(prog, 2, "color");
	glLinkProgram(prog);
	GLint ok = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[512] = {0};
		glGetProgramInfoLog(prog, sizeof log - 1, NULL, log);
		notify("GL test: %s link failed: %s", what, log);
		return 0;
	}
	return prog;
}

typedef struct { float x, y, u, v, r, g, b, a; } Vertex;

static Vertex* quad(Vertex* v, float x, float y, float w, float h, float u0, float v0, float u1, float v1, float alpha) {
	Vertex q[6] = {
		{x, y, u0, v0, 1, 1, 1, alpha},         {x + w, y, u1, v0, 1, 1, 1, alpha}, {x, y + h, u0, v1, 1, 1, 1, alpha},
		{x + w, y, u1, v0, 1, 1, 1, alpha},     {x + w, y + h, u1, v1, 1, 1, 1, alpha}, {x, y + h, u0, v1, 1, 1, 1, alpha},
	};
	memcpy(v, q, sizeof q);
	return v + 6;
}

int main(void) {
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = crash_handler;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
	for (unsigned i = 0; i < sizeof signals / sizeof signals[0]; i++) sigaction(signals[i], &sa, NULL);

	LOG("gl-test: start");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) {
		notify("GL test: SDL_Init failed: %s", SDL_GetError());
		return 1;
	}
	LOG("gl-test: SDL_Init ok");
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	LOG("gl-test: creating window (this loads libOSMesa.so.8 with dlopen)");
	SDL_Window* win = SDL_CreateWindow("gl-test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 1920, 1080,
	                                   SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
	if (!win) {
		notify("GL test: CreateWindow failed: %s", SDL_GetError());
		return 1;
	}
	LOG("gl-test: window created, creating GL context");
	SDL_GLContext ctx = SDL_GL_CreateContext(win);
	if (!ctx) {
		notify("GL test: CreateContext failed: %s", SDL_GetError());
		return 1;
	}
	LOG("gl-test: context ok, loading GL functions");
	load_gl();
	LOG("gl-test: functions loaded (glGetString=%p glCreateShader=%p)", (void*)glGetString, (void*)glCreateShader);
#define CHECK(ret, name, args) if (!name) LOG("gl-test: MISSING function %s", #name);
	GLFUNCS(CHECK)
	notify("GL test: %s / %s", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
	const GLubyte* glsl = glGetString(GL_SHADING_LANGUAGE_VERSION);
	notify("GL test: GLSL %s", glsl ? (const char*)glsl : "(none)");

	int w = 1920, h = 1080;
	SDL_GL_GetDrawableSize(win, &w, &h);

	LOG("gl-test: compiling shaders");
	GLuint sprite_prog = make_program(SPRITE_VS, SPRITE_FS, "sprite");
	GLuint blit_prog = make_program(BLIT_VS, BLIT_FS, "blit");
	if (!sprite_prog || !blit_prog) return 1;

	LOG("gl-test: shaders ok, creating textures");
	/* 512x512 test texture */
	static unsigned char pixels[512 * 512 * 4];
	for (int y = 0; y < 512; y++)
		for (int x = 0; x < 512; x++) {
			unsigned char* p = pixels + (y * 512 + x) * 4;
			int chk = ((x / 16) + (y / 16)) & 1;
			p[0] = (unsigned char)(x / 2); p[1] = (unsigned char)(y / 2); p[2] = chk ? 220 : 60;
			p[3] = ((x % 32) < 28 || (y % 32) < 28) ? 255 : 0;
		}
	GLuint tex, fbo_tex, fbo;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &fbo_tex);
	glBindTexture(GL_TEXTURE_2D, fbo_tex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, GAME_W, GAME_H, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fbo_tex, 0);
	if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
		notify("GL test: framebuffer object incomplete");
		return 1;
	}
	notify("GL test: setup ok, %dx%d output, running %d s", w, h, RUN_SECONDS);

	GLuint vbo;
	glGenBuffers(1, &vbo);
	static Vertex verts[(3 * 20 * 15 + 100 + 1) * 6];

	Uint32 start = SDL_GetTicks(), fps_time = start;
	unsigned frames = 0, total_frames = 0;
	while (SDL_GetTicks() - start < RUN_SECONDS * 1000u) {
		SDL_Event e;
		while (SDL_PollEvent(&e)) {}
		float t = (SDL_GetTicks() - start) / 1000.0f;

		/* build the scene */
		Vertex* v = verts;
		for (int layer = 0; layer < 3; layer++)
			for (int ty = 0; ty < 15; ty++)
				for (int tx = 0; tx < 20; tx++) {
					int idx = (tx * 7 + ty * 3 + layer) % 16;
					float u0 = (idx % 4) * 0.125f, v0 = (idx / 4) * 0.125f;
					v = quad(v, tx * 32.0f, ty * 32.0f, 32, 32, u0, v0, u0 + 0.0625f, v0 + 0.0625f, layer == 0 ? 1.0f : 0.6f);
				}
		for (int i = 0; i < 100; i++) {
			float x = 300 + 280 * sinf(t * 0.7f + i), y = 220 + 200 * cosf(t * 0.9f + i * 1.3f);
			v = quad(v, x, y, 32, 48, 0.5f, 0.5f, 0.5625f, 0.5781f, 1.0f);
		}
		int count = (int)(v - verts);

		/* game pass into the 640x480 framebuffer */
#define STEP(msg) do { if (total_frames == 0) LOG("gl-test: frame 0: %s", msg); } while (0)
		STEP("scene built");
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glViewport(0, 0, GAME_W, GAME_H);
		STEP("fbo bound");
		glClearColor(0.1f, 0.1f, 0.3f, 1);
		glClear(GL_COLOR_BUFFER_BIT);
		STEP("cleared");
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glUseProgram(sprite_prog);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glUniform1i(glGetUniformLocation(sprite_prog, "tex"), 0);
		STEP("sprite program set up");
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, count * sizeof(Vertex), verts, GL_STREAM_DRAW);
		STEP("vertex data uploaded");
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(4 * sizeof(float)));
		glEnableVertexAttribArray(0);
		glEnableVertexAttribArray(1);
		glEnableVertexAttribArray(2);
		STEP("attributes set, drawing scene");
		glDrawArrays(GL_TRIANGLES, 0, count);
		STEP("scene drawn");

		/* present pass: scale the framebuffer texture to the screen */
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
		glViewport(0, 0, w, h);
		glDisable(GL_BLEND);
		glUseProgram(blit_prog);
		glBindTexture(GL_TEXTURE_2D, fbo_tex);
		glUniform1i(glGetUniformLocation(blit_prog, "tex"), 0);
		Vertex b[6];
		quad(b, -1, -1, 2, 2, 0, 0, 1, 1, 1);
		glBufferData(GL_ARRAY_BUFFER, sizeof b, b, GL_STREAM_DRAW);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(2 * sizeof(float)));
		STEP("drawing present pass");
		glDrawArrays(GL_TRIANGLES, 0, 6);
		STEP("present pass drawn, swapping");

		SDL_GL_SwapWindow(win);
		STEP("swapped");
		frames++;
		total_frames++;
		Uint32 now = SDL_GetTicks();
		if (now - fps_time >= 5000) {
			notify("GL test: %.1f fps", frames * 1000.0 / (now - fps_time));
			frames = 0;
			fps_time = now;
		}
	}
	notify("GL test: finished, average %.1f fps over %u frames", total_frames * 1000.0 / (SDL_GetTicks() - start), total_frames);
	SDL_GL_DeleteContext(ctx);
	SDL_DestroyWindow(win);
	SDL_Quit();
	return 0;
}
