/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* Tsukuru Player (the launcher) for the PS5.
 *
 * Lists the game folders found in /data/games and on USB sticks ("games" folder), works out which
 * RPG Maker generation each one is from its files, and starts the matching engine through the
 * websrv launcher API (http://127.0.0.1:8080/hbldr):
 *
 *   RPG Maker 2000 / 2003   ->  EasyRPG Player   (/data/homebrew/easyrpg)
 *   RPG Maker XP / VX / Ace ->  mkxp-z           (/data/homebrew/mkxp-z)
 *
 * Controls: D-pad/left stick move, Cross starts the game, Triangle rescans, L1/R1 page up/down.
 *
 * Copyright (C) 2026 the easyrpg-ps5 authors. GPLv3 or later.
 */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL.h>
#include <SDL_ttf.h>

#define SCREEN_W 1920
#define SCREEN_H 1080

#define FONT_PATH "/data/homebrew/rpgmaker/font.ttf"
#define EASYRPG_ELF "/data/homebrew/easyrpg/eboot.elf"
#define MKXP_ELF "/data/homebrew/mkxp-z/eboot.elf"
#define OUTSIDER_DIR "/data/homebrew/outsider"
#define OUTSIDER_ELF OUTSIDER_DIR "/eboot.elf"

typedef enum {
	ENGINE_EASYRPG,
	ENGINE_XP,
	ENGINE_VX,
	ENGINE_ACE,
	ENGINE_MZ,
	ENGINE_MV,
} Engine;

static const char* engine_label(Engine e) {
	switch (e) {
	case ENGINE_EASYRPG: return "RPG Maker 2000/2003";
	case ENGINE_XP: return "RPG Maker XP";
	case ENGINE_VX: return "RPG Maker VX";
	case ENGINE_ACE: return "RPG Maker VX Ace";
	case ENGINE_MZ: return "RPG Maker MZ";
	case ENGINE_MV: return "RPG Maker MV";
	}
	return "?";
}

typedef struct {
	char name[256];
	char path[600];
	char root[600]; /* the folder the engine is given: the game folder, or its www folder for MV */
	const char* where;
	Engine engine;
	char missing_rtp[128]; /* RTP names the game asks for that are not installed (XP/VX/Ace only) */
	char exec_name[128];   /* XP/VX/Ace: "Uranium" when the game's program is Uranium.exe (reads Uranium.ini); "" for Game.exe */
	int protected_files;   /* MV/MZ: the game's files have scrambled names (its own file protection); it cannot be run */
	int incomplete;        /* MV/MZ: the game's data is there but its js folder is not (a copy that stopped half way) */
} Game;

#define PROTECTED_TEXT "This game's files have scrambled names (its own file protection). That is usually unreadable here. Press Cross again to try anyway."
#define PROTECTED_MESSAGE "! " PROTECTED_TEXT
#define INCOMPLETE_TEXT "This game was not copied completely: its \"js\" folder is missing. Copy the whole game folder to the console again."
#define INCOMPLETE_MESSAGE "! " INCOMPLETE_TEXT


static void collect_rtps(const char* dir, char* joined, size_t joined_size, char* missing, size_t missing_size);
static void easyrpg_rtp_check(const char* dir, char* missing, size_t missing_size);

#define MAX_GAMES 512
static Game games[MAX_GAMES];
static int game_count;

/* --- this console's address (shown so that games can be added from a phone or PC) --- */

static char local_ip[64];

static void find_local_ip(void) {
	local_ip[0] = '\0';
	struct ifaddrs* list = NULL;
	if (getifaddrs(&list) != 0) return;
	for (struct ifaddrs* a = list; a; a = a->ifa_next) {
		if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
		struct sockaddr_in* sin = (struct sockaddr_in*)a->ifa_addr;
		const unsigned char* b = (const unsigned char*)&sin->sin_addr;
		if (b[0] == 127 || (b[0] == 169 && b[1] == 254)) continue; /* loopback, self-assigned */
		snprintf(local_ip, sizeof local_ip, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
		break;
	}
	freeifaddrs(list);
}

/* --- game detection --- */

/* Finds an entry of `dir` whose name matches `name` ignoring case; copies the real name to `out`. */
static int find_entry(const char* dir, const char* name, char* out, size_t out_size, unsigned char* type) {
	DIR* d = opendir(dir);
	if (!d) return 0;
	int found = 0;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (!strcasecmp(e->d_name, name)) {
			snprintf(out, out_size, "%s", e->d_name);
			if (type) *type = e->d_type;
			found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

static int has_entry(const char* dir, const char* name) {
	char tmp[256];
	return find_entry(dir, name, tmp, sizeof tmp, NULL);
}

/* The value of `key` in the game's Game.ini ("Scripts", "RTP", ...). Keys are case-insensitive and
 * may have spaces around the "=" ("scripts =Data\Scripts.rvdata2"). */
static int ini_value_in(const char* dir, const char* file, const char* key, char* out, size_t out_size) {
	char name[256], path[900];
	if (!find_entry(dir, file, name, sizeof name, NULL)) return 0;
	snprintf(path, sizeof path, "%s/%s", dir, name);
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	size_t key_len = strlen(key);
	char line[512];
	int found = 0;
	while (fgets(line, sizeof line, f)) {
		char* p = line;
		while (*p == ' ' || *p == '\t') p++;
		if (strncasecmp(p, key, key_len) != 0) continue;
		p += key_len;
		while (*p == ' ' || *p == '\t') p++;
		if (*p != '=') continue;
		p++;
		while (*p == ' ' || *p == '\t') p++;
		snprintf(out, out_size, "%s", p);
		out[strcspn(out, "\r\n")] = '\0';
		for (size_t n = strlen(out); n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'); n--) out[n - 1] = '\0';
		found = 1;
		break;
	}
	fclose(f);
	return found;
}

/* The ini file of an XP/VX/Ace game: "Game.ini", or "<name>.ini" when the game's program is called something else
 * (Uranium.exe reads Uranium.ini and Uranium.rgssad). */
static int find_game_ini(const char* dir, char* out, size_t out_size) {
	if (has_entry(dir, "Game.ini")) {
		snprintf(out, out_size, "Game.ini");
		return 1;
	}
	DIR* d = opendir(dir);
	if (!d) return 0;
	struct dirent* e;
	int found = 0;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		char scripts[64];
		if (len < 5 || strcasecmp(e->d_name + len - 4, ".ini") != 0) continue;
		if (ini_value_in(dir, e->d_name, "Scripts", scripts, sizeof scripts)) {
			snprintf(out, out_size, "%s", e->d_name);
			found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

static int ini_value(const char* dir, const char* key, char* out, size_t out_size) {
	char name[256];
	if (!find_game_ini(dir, name, sizeof name)) return 0;
	return ini_value_in(dir, name, key, out, out_size);
}

/* Like has_entry(), for a file inside a subfolder of `dir` ("js", "www/js"). Every name is matched ignoring
 * case, since games made on Windows do not care about it. */
static int has_entry_in(const char* dir, const char* sub, const char* name) {
	char cur[700], comp[64], real[256];
	snprintf(cur, sizeof cur, "%s", dir);
	const char* p = sub;
	while (*p) {
		size_t n = strcspn(p, "/");
		snprintf(comp, sizeof comp, "%.*s", (int)n, p);
		if (!find_entry(cur, comp, real, sizeof real, NULL)) return 0;
		size_t len = strlen(cur);
		snprintf(cur + len, sizeof cur - len, "/%s", real);
		p += n;
		if (*p == '/') p++;
	}
	return has_entry(cur, name);
}

static int detect(const char* dir, Engine* engine) {
	if (has_entry(dir, "RPG_RT.ldb") || has_entry(dir, "RPG_RT.lmt") || has_entry(dir, "EASY_RT.edb")) {
		*engine = ENGINE_EASYRPG;
		return 1;
	}
	char scripts[256];
	if (ini_value(dir, "Scripts", scripts, sizeof scripts)) {
		if (strcasestr(scripts, "rvdata2")) { *engine = ENGINE_ACE; return 1; }
		if (strcasestr(scripts, "rvdata")) { *engine = ENGINE_VX; return 1; }
		if (strcasestr(scripts, "rxdata")) { *engine = ENGINE_XP; return 1; }
	}
	if (has_entry(dir, "Game.rgss3a")) { *engine = ENGINE_ACE; return 1; }
	if (has_entry(dir, "Game.rgss2a")) { *engine = ENGINE_VX; return 1; }
	if (has_entry(dir, "Game.rgssad")) { *engine = ENGINE_XP; return 1; }
	/* MZ: js/rmmz_core.js next to data/ and img/. MV: js/rpg_core.js, usually inside a www folder. */
	if (has_entry_in(dir, "js", "rmmz_core.js")) { *engine = ENGINE_MZ; return 1; }
	if (has_entry_in(dir, "js", "rpg_core.js") || has_entry_in(dir, "www/js", "rpg_core.js")) { *engine = ENGINE_MV; return 1; }
	return 0;
}

/* A folder that has an MV/MZ game's data (data/System.json, maybe inside www) but no scripts: a copy that stopped half
 * way. Listed with a warning so it is not simply missing from the list. */
static int detect_incomplete(const char* dir) {
	if (!has_entry_in(dir, "data", "System.json") && !has_entry_in(dir, "www/data", "System.json")) return 0;
	return has_entry(dir, "index.html") || has_entry(dir, "package.json") || has_entry_in(dir, "www", "index.html");
}

/* The folder an MV game's scripts live in: "www" inside the game folder (desktop deployments), or the game
 * folder itself (web deployments). */
static void mv_root(const char* dir, char* out, size_t size) {
	char real[256];
	if (has_entry_in(dir, "www/js", "rpg_core.js") && find_entry(dir, "www", real, sizeof real, NULL)) {
		snprintf(out, size, "%s/%s", dir, real);
	} else {
		snprintf(out, size, "%s", dir);
	}
}

/* MV/MZ games normally have readable image names (img/system/Window.png, or .png_ / .rpgmvp when encrypted). A few
 * commercial games rename every file to a hash (16 hex digits, no extension) and rely on their own player to map them
 * back. Such a game cannot be read by this runtime. */
static int looks_protected(const char* root) {
	char img[256], sys[256], dir[1000];
	if (!find_entry(root, "img", img, sizeof img, NULL)) return 0;
	snprintf(dir, sizeof dir, "%s/%s", root, img);
	if (!find_entry(dir, "system", sys, sizeof sys, NULL)) return 0;
	snprintf(dir, sizeof dir, "%s/%s/%s", root, img, sys);
	DIR* d = opendir(dir);
	if (!d) return 0;
	int total = 0, hashed = 0;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.' || e->d_type == DT_DIR) continue;
		total++;
		size_t len = strlen(e->d_name), i = 0;
		while (i < len && strchr("0123456789abcdefABCDEF", e->d_name[i])) i++;
		if (len == 16 && i == len) hashed++;
	}
	closedir(d);
	return total >= 5 && hashed * 2 > total;
}

static int compare_games(const void* a, const void* b) {
	return strcasecmp(((const Game*)a)->name, ((const Game*)b)->name);
}

/* Looks for games below `dir`. Downloaded games are often wrapped in an extra folder (with a readme
 * next to the real game folder), so search up to `depth` levels. `rel` is the path shown in the list. */
static void scan_dir(const char* dir, const char* rel, const char* where, int depth) {
	DIR* d = opendir(dir);
	if (!d) return;
	struct dirent* e;
	while ((e = readdir(d)) && game_count < MAX_GAMES) {
		if (e->d_name[0] == '.' || e->d_type != DT_DIR) continue;
		char path[600], name[256];
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		snprintf(name, sizeof name, "%s%s%s", rel, rel[0] ? " / " : "", e->d_name);
		Game* g = &games[game_count];
		int is_game = detect(path, &g->engine);
		int is_incomplete = !is_game && detect_incomplete(path);
		if (is_incomplete) g->engine = ENGINE_MV;
		if (is_game || is_incomplete) {
			g->incomplete = is_incomplete;
			snprintf(g->path, sizeof g->path, "%s", path);
			if (g->engine == ENGINE_MV) mv_root(path, g->root, sizeof g->root);
			else snprintf(g->root, sizeof g->root, "%s", path);
			g->protected_files = (g->engine == ENGINE_MV || g->engine == ENGINE_MZ) ? looks_protected(g->root) : 0;
			snprintf(g->name, sizeof g->name, "%s", name);
			g->where = where;
			g->missing_rtp[0] = '\0';
			g->exec_name[0] = '\0';
			if (g->engine == ENGINE_XP || g->engine == ENGINE_VX || g->engine == ENGINE_ACE) {
				char ini[256];
				if (find_game_ini(path, ini, sizeof ini) && strcasecmp(ini, "Game.ini") != 0) {
					snprintf(g->exec_name, sizeof g->exec_name, "%.*s", (int)strlen(ini) - 4, ini);
				}
			}
			if (g->engine == ENGINE_XP || g->engine == ENGINE_VX || g->engine == ENGINE_ACE) {
				char joined[1500];
				collect_rtps(path, joined, sizeof joined, g->missing_rtp, sizeof g->missing_rtp);
			} else if (g->engine == ENGINE_EASYRPG) {
				easyrpg_rtp_check(path, g->missing_rtp, sizeof g->missing_rtp);
			}
			game_count++;
		} else if (depth > 1) {
			scan_dir(path, name, where, depth - 1);
		}
	}
	closedir(d);
}

static void scan_root(const char* root, const char* where) {
	scan_dir(root, "", where, 2);
}

static void scan_games(void) {
	game_count = 0;
	char root[64];
	for (int i = 0; i < 8; i++) {
		snprintf(root, sizeof root, "/mnt/usb%d/games", i);
		scan_root(root, "USB");
	}
	scan_root("/data/games", "Console");
	qsort(games, game_count, sizeof(Game), compare_games);
}

/* --- starting a game through websrv --- */

/* websrv splits arguments and environment on spaces; a backslash keeps a space (or a backslash) in. */
static void escape_arg(const char* in, char* out, size_t size) {
	size_t o = 0;
	for (; *in && o + 2 < size; in++) {
		if (*in == ' ' || *in == '\\') out[o++] = '\\';
		out[o++] = *in;
	}
	out[o] = '\0';
}

static void url_encode(const char* in, char* out, size_t size) {
	static const char hex[] = "0123456789ABCDEF";
	size_t o = 0;
	for (; *in && o + 4 < size; in++) {
		unsigned char c = (unsigned char)*in;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
		    c == '.' || c == '/') {
			out[o++] = (char)c;
		} else {
			out[o++] = '%';
			out[o++] = hex[c >> 4];
			out[o++] = hex[c & 15];
		}
	}
	out[o] = '\0';
}

static int http_get(const char* target, char* status, size_t status_size) {
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(8080);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (connect(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
		snprintf(status, status_size, "cannot reach the web launcher on port 8080 (%s). Run start-ps5.bat on your PC.",
		         strerror(errno));
		close(fd);
		return -1;
	}
	char req[13000];
	int n = snprintf(req, sizeof req, "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", target);
	send(fd, req, n, 0);
	char buf[256] = {0};
	recv(fd, buf, sizeof buf - 1, 0); /* the launcher is usually killed before it can answer */
	close(fd);
	snprintf(status, status_size, "%.60s", buf);
	return 0;
}

/* --- RTP: RPG Maker's shared default graphics and sounds ---
 * Expected in /data/rtp/<name> (or rtp/<name> on a USB stick): "2000" and "2003" for EasyRPG, and
 * the name a game asks for in Game.ini for mkxp-z ("Standard" for XP, "RPGVX", "RPGVXAce"). */
static int find_rtp(const char* name, char* out, size_t size) {
	char root[64], real[256];
	unsigned char type = 0;
	for (int i = 0; i < 9; i++) {
		if (i < 8) snprintf(root, sizeof root, "/mnt/usb%d/rtp", i);
		else snprintf(root, sizeof root, "/data/rtp");
		if (find_entry(root, name, real, sizeof real, &type) && type == DT_DIR) {
			snprintf(out, size, "%s/%s", root, real);
			return 1;
		}
	}
	return 0;
}

/* The SoundFont used for MIDI music in XP/VX/Ace games: the first .sf2 in "soundfonts" on a USB
 * stick or in /data/soundfonts. Without one MIDI music is silent. */
static int find_soundfont(char* out, size_t size) {
	char root[64];
	for (int i = 0; i < 9; i++) {
		if (i < 8) snprintf(root, sizeof root, "/mnt/usb%d/soundfonts", i);
		else snprintf(root, sizeof root, "/data/soundfonts");
		DIR* d = opendir(root);
		if (!d) continue;
		struct dirent* e;
		while ((e = readdir(d))) {
			size_t len = strlen(e->d_name);
			if (len > 4 && !strcasecmp(e->d_name + len - 4, ".sf2")) {
				snprintf(out, size, "%s/%s", root, e->d_name);
				closedir(d);
				return 1;
			}
		}
		closedir(d);
	}
	return 0;
}

/* The RTPs an XP/VX/Ace game names in Game.ini (RTP, RTP1, RTP2, RTP3): the folders found go into
 * `joined` (separated by ':'), the names that are not installed into `missing`. */
static void collect_rtps(const char* dir, char* joined, size_t joined_size, char* missing, size_t missing_size) {
	static const char* const keys[] = {"RTP", "RTP1", "RTP2", "RTP3"};
	joined[0] = missing[0] = '\0';
	for (int i = 0; i < 4; i++) {
		char name[128], path[600];
		if (!ini_value(dir, keys[i], name, sizeof name) || !name[0]) continue;
		if (find_rtp(name, path, sizeof path)) {
			size_t len = strlen(joined);
			snprintf(joined + len, joined_size - len, "%s%s", len ? ":" : "", path);
		} else {
			size_t len = strlen(missing);
			snprintf(missing + len, missing_size - len, "%s%s", len ? ", " : "", name);
		}
	}
}

/* RPG Maker 2000/2003 games say in RPG_RT.ini whether they carry the RTP files themselves
 * (FullPackageFlag=1). If they do not, and neither RTP is installed, the game probably misses graphics or
 * sounds: `missing` gets "2000 or 2003" (which of the two depends on the RPG Maker the game was made with). */
static void easyrpg_rtp_check(const char* dir, char* missing, size_t missing_size) {
	char value[32], path[600];
	missing[0] = '\0';
	if (ini_value_in(dir, "RPG_RT.ini", "FullPackageFlag", value, sizeof value) && value[0] == '1') return;
	if (find_rtp("2000", path, sizeof path) || find_rtp("2003", path, sizeof path)) return;
	snprintf(missing, missing_size, "2000 or 2003");
}

/* The engines write their output to this file (MKXP_LOG / RMMZ_LOG) so that the next start of the launcher can
 * say why a game closed. */
#define LAST_RUN_LOG "/data/homebrew/last-run.log"
#define LAST_RUN_LOG_KEPT "/data/homebrew/last-run.prev.log"
#define MKXP_PRELOAD_RB "/data/homebrew/mkxp-z/rgss_compat.rb"

/* If the game that was run last reported an error, puts the last such line into `message`. */
static void last_run_message(char* message, size_t size) {
	FILE* f = fopen(LAST_RUN_LOG, "r");
	if (!f) return;
	char line[700], found[700] = "";
	while (fgets(line, sizeof line, f)) {
		if (strstr(line, "CRASH") || strstr(line, "Exception") || strstr(line, "Error)") || strstr(line, "Error:") ||
		    strcasestr(line, "unable to") || strcasestr(line, "cannot")) {
			line[strcspn(line, "\r\n")] = '\0';
			snprintf(found, sizeof found, "%s", line);
		}
	}
	fclose(f);
	/* kept (not deleted) so that a problem can be looked at afterwards, by the player or in a bug report */
	rename(LAST_RUN_LOG, LAST_RUN_LOG_KEPT);
	if (found[0]) snprintf(message, size, "The last game reported: %s", found);
}

/* --- deleting a game from the console --- */

/* Deleting a big game takes a while (tens of thousands of files), so it runs in its own thread and the list keeps
 * drawing a progress bar. The thread only touches these counters; everything else stays in the main thread. */
static SDL_atomic_t del_total, del_done, del_finished, del_result;
static char del_path[1400];
static SDL_Thread* del_thread;
static int delete_busy;      /* main thread only: a deletion is running */
static int delete_permille;  /* progress for the bar, 0..1000 */

/* Number of files and folders below `path` (folders included, `path` itself not). */
static int count_entries(const char* path, int depth) {
	DIR* d = opendir(path);
	if (!d || depth > 24) {
		if (d) closedir(d);
		return 0;
	}
	int n = 0;
	struct dirent* e;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		char child[1400];
		struct stat st;
		snprintf(child, sizeof child, "%s/%s", path, e->d_name);
		n++;
		if (lstat(child, &st) == 0 && S_ISDIR(st.st_mode)) n += count_entries(child, depth + 1);
	}
	closedir(d);
	return n;
}

/* Removes a file or a whole folder. Returns 0 when everything is gone. */
static int remove_tree(const char* path, int depth) {
	struct stat st;
	if (depth > 24 || lstat(path, &st) != 0) return -1;
	if (!S_ISDIR(st.st_mode)) {
		int r = unlink(path);
		SDL_AtomicAdd(&del_done, 1);
		return r;
	}
	int rc = 0;
	/* Entries are removed while the folder is read, so read it again until nothing is left. */
	for (int pass = 0; pass < 50; pass++) {
		DIR* d = opendir(path);
		if (!d) return -1;
		int seen = 0;
		struct dirent* e;
		while ((e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char child[1400];
			snprintf(child, sizeof child, "%s/%s", path, e->d_name);
			seen++;
			if (remove_tree(child, depth + 1) != 0) rc = -1;
		}
		closedir(d);
		if (!seen) break;
	}
	if (rmdir(path) != 0) rc = -1;
	SDL_AtomicAdd(&del_done, 1);
	return rc;
}

static int delete_worker(void* arg) {
	(void)arg;
	SDL_AtomicSet(&del_total, count_entries(del_path, 0) + 1);
	SDL_AtomicSet(&del_result, remove_tree(del_path, 0));
	SDL_AtomicSet(&del_finished, 1);
	return 0;
}

/* Starts deleting `path` in the background. Returns 0 when the thread runs. */
static int delete_start(const char* path) {
	snprintf(del_path, sizeof del_path, "%s", path);
	SDL_AtomicSet(&del_total, 0);
	SDL_AtomicSet(&del_done, 0);
	SDL_AtomicSet(&del_finished, 0);
	SDL_AtomicSet(&del_result, -1);
	del_thread = SDL_CreateThread(delete_worker, "delete", NULL);
	delete_busy = del_thread != NULL;
	delete_permille = 0;
	return del_thread ? 0 : -1;
}

/* Only folders below /data/games (the console's own storage) may be deleted from here; never anything on a USB stick. */
static int can_delete(const Game* g) {
	return strncmp(g->path, "/data/games/", 12) == 0 && strlen(g->path) > 12 && !strstr(g->path, "..");
}

/* Returns 0 when the engine was started, -1 on error and 1 when the game needs an RTP that is not
 * installed and `confirmed` is not set (the caller asks the player to press Cross again). */
static int start_game(const Game* g, int confirmed, char* message, size_t message_size) {
	int web_engine = g->engine == ENGINE_MZ || g->engine == ENGINE_MV; /* MV and MZ run on the same runtime */
	const char* elf = g->engine == ENGINE_EASYRPG ? EASYRPG_ELF : web_engine ? OUTSIDER_ELF : MKXP_ELF;
	const char* engine_name = g->engine == ENGINE_EASYRPG ? "EasyRPG Player" : web_engine ? "Outsider" : "mkxp-z";
	if (access(elf, F_OK) != 0) {
		snprintf(message, message_size, "%s is not installed (%s is missing).", engine_name, elf);
		return -1;
	}

	if (g->incomplete) {
		snprintf(message, message_size, "%s", INCOMPLETE_TEXT);
		return -1;
	}

	if (g->protected_files && !confirmed) {
		snprintf(message, message_size, "%s", PROTECTED_TEXT);
		return 1;
	}

	char path_esc[1200], args[1400], env[2600];
	escape_arg(web_engine ? g->root : g->path, path_esc, sizeof path_esc);
	args[0] = env[0] = '\0';
	if (g->engine == ENGINE_EASYRPG) {
		if (g->missing_rtp[0] && !confirmed) {
			snprintf(message, message_size,
			         "This game probably needs the RTP \"2000\" or \"2003\" (the one for the RPG Maker it was made with), which is not "
			         "installed. Put that folder into a folder named \"rtp\" on your USB stick (or /data/rtp). Press Cross again to start anyway.");
			return 1;
		}
		snprintf(args, sizeof args, "--project-path %s", path_esc);
		snprintf(env, sizeof env, "HOME=/data/homebrew/easyrpg");
		char rtp[600], rtp_esc[1200];
		if (find_rtp("2000", rtp, sizeof rtp)) {
			escape_arg(rtp, rtp_esc, sizeof rtp_esc);
			size_t len = strlen(env);
			snprintf(env + len, sizeof env - len, " RPG2K_RTP_PATH=%s", rtp_esc);
		}
		if (find_rtp("2003", rtp, sizeof rtp)) {
			escape_arg(rtp, rtp_esc, sizeof rtp_esc);
			size_t len = strlen(env);
			snprintf(env + len, sizeof env - len, " RPG2K3_RTP_PATH=%s", rtp_esc);
		}
	} else if (web_engine) {
		/* MV and MZ games: the runtime needs no RTP. Names in the game are matched ignoring case (Windows games
		 * rely on that) and encrypted images and sounds are decrypted as they are read. */
		snprintf(args, sizeof args, "--game %s --shims " OUTSIDER_DIR "/shims --perf 600", path_esc);
		snprintf(env, sizeof env, "HOME=" OUTSIDER_DIR " PS5PATH_CASE_INSENSITIVE=1 RMMZ_LOG=" LAST_RUN_LOG);
	} else {
		char joined[1500], missing[300], joined_esc[3000];
		collect_rtps(g->path, joined, sizeof joined, missing, sizeof missing);
		if (missing[0] && !confirmed) {
			snprintf(message, message_size,
			         "This game needs the RTP \"%s\", which is not installed. Put the folder \"%s\" into a folder named \"rtp\" on your USB stick "
			         "(or /data/rtp). Press Cross again to start anyway.",
			         missing, missing);
			return 1;
		}
		snprintf(env, sizeof env, "HOME=/data/homebrew/mkxp-z SRCDIR=%s MKXP_LOG=" LAST_RUN_LOG, path_esc);
		if (g->exec_name[0]) {
			char exec_esc[300];
			escape_arg(g->exec_name, exec_esc, sizeof exec_esc);
			size_t len = strlen(env);
			snprintf(env + len, sizeof env - len, " MKXP_EXECNAME=%s", exec_esc);
		}
		if (access(MKXP_PRELOAD_RB, R_OK) == 0) {
			size_t len = strlen(env);
			snprintf(env + len, sizeof env - len, " MKXP_PRELOAD=" MKXP_PRELOAD_RB);
		}
		if (joined[0]) {
			escape_arg(joined, joined_esc, sizeof joined_esc);
			size_t len = strlen(env);
			snprintf(env + len, sizeof env - len, " MKXP_RTP=%s", joined_esc);
		}
		char soundfont[600], sf_esc[1200];
		if (find_soundfont(soundfont, sizeof soundfont)) {
			escape_arg(soundfont, sf_esc, sizeof sf_esc);
			size_t len = strlen(env);
			snprintf(env + len, sizeof env - len, " MKXP_SOUNDFONT=%s", sf_esc);
		}
	}

	char elf_url[256], args_url[3000], env_url[8000], target[12000];
	url_encode(elf, elf_url, sizeof elf_url);
	url_encode(args, args_url, sizeof args_url);
	url_encode(env, env_url, sizeof env_url);
	snprintf(target, sizeof target, "/hbldr?path=%s&args=%s&env=%s", elf_url, args_url, env_url);

	char status[128];
	if (http_get(target, status, sizeof status) < 0) {
		snprintf(message, message_size, "Could not start %s: %s", engine_name, status);
		return -1;
	}
	snprintf(message, message_size, "Starting %s ...", engine_name);
	return 0;
}

/* --- settings (button mapping for RPG Maker MV and MZ games and a few options) --- */

/* The file the launcher writes and the MZ runtime (Outsider) reads at start-up. */
#define SETTINGS_PATH "/data/homebrew/outsider/settings.ini"

/* What a pad button can be assigned to: a "virtual" gamepad button as RPG Maker's scripts know them. */
enum { ACT_CONFIRM, ACT_CANCEL, ACT_DASH, ACT_MENU, ACT_PAGEUP, ACT_PAGEDOWN, ACT_L2, ACT_R2, ACT_SELECT,
       ACT_START, ACT_L3, ACT_R3, ACT_COUNT };
static const char* const action_names[ACT_COUNT + 1] = {
	"Confirm", "Cancel", "Dash", "Menu", "Page up", "Page down", "L2 (extra)", "R2 (extra)", "Select (extra)",
	"Start (extra)", "L3 (extra)", "R3 (extra)", "Nothing",
};

typedef struct {
	const char* key;   /* name in settings.ini */
	const char* label; /* shown in the menu */
	int def;           /* default action */
} PadButton;
static const PadButton pad_buttons[] = {
	{"cross", "Cross  \xE2\x9C\x95", ACT_CONFIRM},
	{"circle", "Circle  \xE2\x97\x8B", ACT_CANCEL},
	{"square", "Square  \xE2\x96\xA1", ACT_DASH},
	{"triangle", "Triangle  \xE2\x96\xB3", ACT_MENU},
	{"l1", "L1", ACT_PAGEUP},
	{"r1", "R1", ACT_PAGEDOWN},
	{"l2", "L2", ACT_L2},
	{"r2", "R2", ACT_R2},
	{"l3", "L3  (press the left stick)", ACT_L3},
	{"r3", "R3  (press the right stick)", ACT_R3},
	{"options", "Options", ACT_START},
	{"touchpad", "Touchpad press", ACT_SELECT},
};
#define PAD_BUTTONS ((int)(sizeof pad_buttons / sizeof pad_buttons[0]))

/* Keyboard keys a pad button can press in the game (DOM key codes, as the game's scripts see them). */
typedef struct { int code; const char* label; } KeyDef;
static KeyDef key_table[100];
static int key_table_size;

static void key_table_init(void) {
	static char names[80][8];
	int n = 0, used = 0;
	key_table[n++] = (KeyDef){0, "none"};
	static const KeyDef special[] = {
		{13, "Enter"}, {27, "Escape"}, {32, "Space"}, {9, "Tab"}, {16, "Shift"}, {17, "Ctrl"}, {18, "Alt"},
		{8, "Backspace"}, {38, "Up arrow"}, {40, "Down arrow"}, {37, "Left arrow"}, {39, "Right arrow"},
		{33, "Page up"}, {34, "Page down"}, {36, "Home"}, {35, "End"}, {45, "Insert"}, {46, "Delete"},
	};
	for (size_t i = 0; i < sizeof special / sizeof special[0]; i++) key_table[n++] = special[i];
	for (int c = 'A'; c <= 'Z'; c++) {
		snprintf(names[used], sizeof names[used], "%c", c);
		key_table[n++] = (KeyDef){c, names[used++]};
	}
	for (int c = '0'; c <= '9'; c++) {
		snprintf(names[used], sizeof names[used], "%c", c);
		key_table[n++] = (KeyDef){c, names[used++]};
	}
	for (int f = 1; f <= 12; f++) {
		snprintf(names[used], sizeof names[used], "F%d", f);
		key_table[n++] = (KeyDef){111 + f, names[used++]};
	}
	key_table_size = n;
}

static int key_index(int code) {
	for (int i = 0; i < key_table_size; i++)
		if (key_table[i].code == code) return i;
	return 0;
}

/* What a pad button does, as one ready-made choice: the game button it presses, and the keyboard key (if any) that it
 * presses as well. The list is in order of importance; the screen cycles through it. */
typedef struct {
	const char* name;
	const char* hint;
	int map; /* action as the game sees it; -1 nothing; -2 this button's own extra (for plugins) */
	int key; /* DOM key code, 0 = none */
} Role;
static const Role roles[] = {
	{"Confirm", "OK in menus and dialogs. Also presses the Z key.", ACT_CONFIRM, 90},
	{"Cancel", "Go back / close a menu. Also presses the X key.", ACT_CANCEL, 88},
	{"Dash", "Hold to run (Shift).", ACT_DASH, 0},
	{"Menu", "Opens the game's menu.", ACT_MENU, 0},
	{"Page up", "Previous tab or page (Q).", ACT_PAGEUP, 0},
	{"Page down", "Next tab or page (W).", ACT_PAGEDOWN, 0},
	{"Skip text", "Hold to skip messages (the Ctrl key), in games that support it.", -1, 17},
	{"Auto mode", "Switches automatic message advance on and off, in games that have it (the A key).", -1, 65},
	{"Hide the game's UI", "Hides the interface for a clean view, in games that have it (the Tab key).", -1, 9},
	{"Enter key", "Presses Enter on the keyboard.", -1, 13},
	{"Escape key", "Presses Escape on the keyboard.", -1, 27},
	{"Space key", "Presses the space bar.", -1, 32},
	{"Extra button", "A spare button that a few plugins read. Does nothing in most games.", -2, 0},
	{"Nothing", "This button does nothing.", -1, 0},
};
enum { R_CONFIRM, R_CANCEL, R_DASH, R_MENU, R_PAGEUP, R_PAGEDOWN, R_SKIP, R_AUTO, R_HIDEUI, R_ENTER, R_ESC, R_SPACE,
       R_EXTRA, R_NOTHING, ROLE_COUNT };

/* The layout that comes already set up, per pad button (order of pad_buttons). */
static const int recommended_role[] = {R_CONFIRM, R_CANCEL, R_DASH, R_MENU, R_PAGEUP, R_PAGEDOWN, R_AUTO, R_SKIP,
                                       R_EXTRA, R_HIDEUI, R_EXTRA, R_EXTRA};
/* The order the screen lists the buttons in: the ones every game uses first. */
static const int ui_order[] = {0, 1, 2, 3, 4, 5, 6, 7, 10, 11, 8, 9};

static int role_available(int button, int role) {
	return role != R_EXTRA || pad_buttons[button].def >= ACT_L2; /* only buttons that have a spare "extra" slot */
}

static int role_map(int button, int role) {
	return roles[role].map == -2 ? pad_buttons[button].def : roles[role].map;
}

static const char* const scale_names[] = {"Automatic", "Sharp (blocky pixels)", "Smooth"};
static const char* const pointer_names[] = {"Slow", "Normal", "Fast"};

typedef struct {
	int map[PAD_BUTTONS]; /* action per pad button, -1 = nothing */
	int key[PAD_BUTTONS]; /* keyboard key (DOM key code) a pad button also presses, 0 = none */
	int scale;            /* 0 automatic, 1 sharp, 2 smooth */
	int pointer;          /* touchpad pointer speed: 0 slow, 1 normal, 2 fast */
} Settings;
static Settings settings;

static void settings_defaults(Settings* st) {
	for (int i = 0; i < PAD_BUTTONS; i++) {
		st->map[i] = role_map(i, recommended_role[i]);
		st->key[i] = roles[recommended_role[i]].key;
	}
	st->scale = 0;
	st->pointer = 1;
}

static void settings_load(void) {
	settings_defaults(&settings);
	FILE* f = fopen(SETTINGS_PATH, "r");
	if (!f) return;
	char line[128];
	while (fgets(line, sizeof line, f)) {
		char* eq = strchr(line, '=');
		if (!eq) continue;
		*eq = '\0';
		int value = atoi(eq + 1);
		if (!strcmp(line, "scale")) {
			if (value >= 0 && value <= 2) settings.scale = value;
		} else if (!strcmp(line, "pointer")) {
			if (value >= 0 && value <= 2) settings.pointer = value;
		} else if (!strncmp(line, "map.", 4)) {
			for (int i = 0; i < PAD_BUTTONS; i++)
				if (!strcmp(line + 4, pad_buttons[i].key) && value >= -1 && value < ACT_COUNT) settings.map[i] = value;
		} else if (!strncmp(line, "key.", 4)) {
			for (int i = 0; i < PAD_BUTTONS; i++)
				if (!strcmp(line + 4, pad_buttons[i].key) && key_index(value) > 0) settings.key[i] = value;
		}
	}
	fclose(f);
}

static void settings_save(void) {
	FILE* f = fopen(SETTINGS_PATH, "w");
	if (!f) return;
	fprintf(f, "# written by the Tsukuru Player launcher (Settings)\n");
	for (int i = 0; i < PAD_BUTTONS; i++) fprintf(f, "map.%s=%d\n", pad_buttons[i].key, settings.map[i]);
	for (int i = 0; i < PAD_BUTTONS; i++) fprintf(f, "key.%s=%d\n", pad_buttons[i].key, settings.key[i]);
	fprintf(f, "scale=%d\npointer=%d\n", settings.scale, settings.pointer);
	fclose(f);
}

#define ROW_SCALE PAD_BUTTONS
#define SETTINGS_ROWS (PAD_BUTTONS + 3) /* the buttons, scaling, pointer speed, reset */

/* Which role a pad button has now, or -1 when the saved values do not match one (older settings files). */
static int role_of(int button) {
	for (int r = 0; r < ROLE_COUNT; r++)
		if (role_available(button, r) && settings.map[button] == role_map(button, r) && settings.key[button] == roles[r].key)
			return r;
	return -1;
}

static void role_cycle(int button, int dir) {
	int cur = role_of(button), r = cur;
	for (int n = 0; n < ROLE_COUNT; n++) {
		r = r < 0 ? (dir > 0 ? 0 : ROLE_COUNT - 1) : ((r + (dir > 0 ? 1 : -1)) % ROLE_COUNT + ROLE_COUNT) % ROLE_COUNT;
		if (role_available(button, r)) break;
	}
	settings.map[button] = role_map(button, r);
	settings.key[button] = roles[r].key;
}

/* Changes the value of a row one step forwards (dir > 0) or backwards. */
static void settings_change(int row, int dir) {
	if (row < PAD_BUTTONS) {
		role_cycle(ui_order[row], dir);
	} else if (row == ROW_SCALE) {
		settings.scale = (settings.scale + (dir > 0 ? 1 : 2)) % 3;
	} else if (row == ROW_SCALE + 1) {
		settings.pointer = (settings.pointer + (dir > 0 ? 1 : 2)) % 3;
	} else {
		settings_defaults(&settings);
	}
	settings_save();
}

/* --- drawing --- */

static SDL_Renderer* ren;
static const char* shot_path; /* development aid: RPGMAKER_SHOT=file.ppm draws one screen, saves it and quits */

static void maybe_shot(void) {
	if (!shot_path) return;
	int w = SCREEN_W, h = SCREEN_H;
	unsigned char* px = malloc((size_t)w * h * 3);
	if (px && SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_RGB24, px, w * 3) == 0) {
		FILE* f = fopen(shot_path, "wb");
		if (f) {
			fprintf(f, "P6\n%d %d\n255\n", w, h);
			fwrite(px, 1, (size_t)w * h * 3, f);
			fclose(f);
		}
	}
	free(px);
	exit(0);
}
static TTF_Font *font_big, *font_mid, *font_small;

static int text_width(TTF_Font* f, const char* s) {
	int w = 0, h = 0;
	TTF_SizeUTF8(f, s, &w, &h);
	return w;
}

static void draw_text(TTF_Font* f, const char* s, int x, int y, SDL_Color c) {
	if (!s[0]) return;
	SDL_Surface* surf = TTF_RenderUTF8_Blended(f, s, c);
	if (!surf) return;
	SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
	SDL_Rect dst = {x, y, surf->w, surf->h};
	SDL_FreeSurface(surf);
	if (tex) {
		SDL_RenderCopy(ren, tex, NULL, &dst);
		SDL_DestroyTexture(tex);
	}
}

/* Text cut off with "..." so it fits into `max_w` pixels. */
static void draw_text_fit(TTF_Font* f, const char* s, int x, int y, int max_w, SDL_Color c) {
	char buf[300];
	snprintf(buf, sizeof buf, "%s", s);
	while (text_width(f, buf) > max_w && strlen(buf) > 4) {
		size_t len = strlen(buf);
		/* remove one UTF-8 character (skip continuation bytes) before the "..." */
		size_t cut = len - 3;
		while (cut > 0 && ((unsigned char)buf[cut - 1] & 0xC0) == 0x80) cut--;
		if (cut > 0) cut--;
		while (cut > 0 && ((unsigned char)buf[cut] & 0xC0) == 0x80) cut--;
		snprintf(buf + cut, sizeof buf - cut, "...");
	}
	draw_text(f, buf, x, y, c);
}

static void fill(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b) {
	SDL_SetRenderDrawColor(ren, r, g, b, 255);
	SDL_Rect rc = {x, y, w, h};
	SDL_RenderFillRect(ren, &rc);
}

#define ROW_H 62
#define LIST_TOP 200
#define VISIBLE_ROWS 11  /* rows end at y=882; the footer bar starts at y=970 */

static void draw_screen(int selected, int scroll, const char* message) {
	static const SDL_Color white = {235, 238, 250, 255}, dim = {150, 160, 190, 255}, gold = {255, 210, 90, 255};

	fill(0, 0, SCREEN_W, SCREEN_H, 16, 20, 42);
	fill(0, 0, SCREEN_W, 150, 26, 34, 74);
	draw_text(font_big, "Tsukuru Player", 90, 30, white);
	char sub[128];
	snprintf(sub, sizeof sub, "%d game%s", game_count, game_count == 1 ? "" : "s");
	draw_text(font_mid, sub, SCREEN_W - 90 - text_width(font_mid, sub), 62, dim);

	if (game_count == 0) {
		draw_text(font_mid, "No games found.", 90, LIST_TOP + 20, white);
		draw_text(font_small, "Copy game folders to /data/games (FTP), or to a folder named \"games\" on a USB stick.", 90,
		          LIST_TOP + 90, dim);
		draw_text(font_small, "Supported: RPG Maker 2000, 2003, XP, VX, VX Ace, MV and MZ.", 90, LIST_TOP + 140, dim);
		if (local_ip[0]) {
			char line[200];
			snprintf(line, sizeof line, "From a phone or PC: start ftpsrv, then connect an FTP app to  %s  port 2121.", local_ip);
			draw_text(font_small, line, 90, LIST_TOP + 190, dim);
		}
	}

	for (int row = 0; row < VISIBLE_ROWS; row++) {
		int i = scroll + row;
		if (i >= game_count) break;
		int y = LIST_TOP + row * ROW_H;
		int usable = 1;
		if (i == selected) fill(60, y, SCREEN_W - 120, ROW_H - 6, 52, 84, 196);
		if (games[i].missing_rtp[0] || games[i].protected_files || games[i].incomplete) draw_text(font_mid, "!", 68, y + 6, (SDL_Color){255, 120, 90, 255});
		SDL_Color c = usable ? white : dim;
		draw_text_fit(font_mid, games[i].name, 90, y + 6, 1000, c);
		draw_text(font_small, engine_label(games[i].engine), 1130, y + 12, usable ? gold : dim);
		draw_text(font_small, games[i].where, SCREEN_W - 90 - text_width(font_small, games[i].where), y + 12, dim);
	}
	if (game_count > VISIBLE_ROWS) {
		char pos[64];
		snprintf(pos, sizeof pos, "%d / %d", selected + 1, game_count);
		draw_text(font_small, pos, SCREEN_W - 90 - text_width(font_small, pos), LIST_TOP + VISIBLE_ROWS * ROW_H + 10, dim);
	}

	fill(0, SCREEN_H - 110, SCREEN_W, 110, 26, 34, 74);
	draw_text(font_small, "\xE2\x9C\x95  Play     \xE2\x96\xB3  Rescan     \xE2\x96\xA1  Delete (console games)     L1 / R1  Page     Options  Settings", 90,
	          SCREEN_H - 92, dim);
	if (local_ip[0]) {
		char addr[100];
		snprintf(addr, sizeof addr, "Console: %s", local_ip);
		draw_text(font_small, addr, SCREEN_W - 90 - text_width(font_small, addr), SCREEN_H - 92, dim);
	}
	if (message[0]) draw_text_fit(font_small, message, 90, SCREEN_H - 50, SCREEN_W - 180, gold);
	if (delete_busy) {
		fill(90, SCREEN_H - 16, SCREEN_W - 180, 8, 52, 60, 110);
		fill(90, SCREEN_H - 16, (SCREEN_W - 180) * delete_permille / 1000, 8, 255, 210, 90);
	}
	maybe_shot();
	SDL_RenderPresent(ren);
}

#define SETTINGS_TOP 160
#define SETTINGS_ROW_H 52
#define SETTINGS_VISIBLE 15

static void draw_settings(int row, int scroll, const char* message) {
	static const SDL_Color white = {245, 247, 255, 255}, soft = {205, 212, 235, 255}, gold = {255, 210, 90, 255};

	fill(0, 0, SCREEN_W, SCREEN_H, 16, 20, 42);
	fill(0, 0, SCREEN_W, 140, 26, 34, 74);
	draw_text(font_big, "Controls", 90, 14, white);
	draw_text(font_small, "For RPG Maker MV and MZ games. Each button does one thing; the ones every game uses come first.", 90, 98, soft);

	const char* hint = message;
	for (int r = 0; r < SETTINGS_VISIBLE; r++) {
		int i = scroll + r;
		if (i >= SETTINGS_ROWS) break;
		int y = SETTINGS_TOP + r * SETTINGS_ROW_H;
		if (i == row) fill(60, y, SCREEN_W - 120, SETTINGS_ROW_H - 4, 52, 84, 196);
		if (i < PAD_BUTTONS) {
			int b = ui_order[i];
			int role = role_of(b);
			draw_text(font_mid, pad_buttons[b].label, 100, y + 2, white);
			SDL_Color c = role == recommended_role[b] ? white : gold;
			draw_text(font_mid, role >= 0 ? roles[role].name : "Custom (older setting)", 760, y + 2, c);
			if (i == row) hint = role >= 0 ? roles[role].hint : "A mix from an older settings file. Press left or right to pick a ready-made choice.";
		} else if (i == ROW_SCALE) {
			draw_text(font_mid, "Picture scaling", 100, y + 2, white);
			draw_text(font_mid, scale_names[settings.scale], 760, y + 2, settings.scale ? gold : white);
		} else if (i == ROW_SCALE + 1) {
			draw_text(font_mid, "Touchpad pointer speed", 100, y + 2, white);
			draw_text(font_mid, pointer_names[settings.pointer], 760, y + 2, settings.pointer != 1 ? gold : white);
		} else {
			draw_text(font_mid, "Back to the recommended layout", 100, y + 2, soft);
			if (i == row) hint = "Sets every button above, the scaling and the pointer speed back to how they came.";
		}
	}
	if (scroll > 0) draw_text(font_small, "\xE2\x96\xB2 more", SCREEN_W - 260, SETTINGS_TOP - 34, soft);
	if (scroll + SETTINGS_VISIBLE < SETTINGS_ROWS)
		draw_text(font_small, "\xE2\x96\xBC more", SCREEN_W - 260, SETTINGS_TOP + SETTINGS_VISIBLE * SETTINGS_ROW_H - 4, soft);

	fill(0, SCREEN_H - 110, SCREEN_W, 110, 26, 34, 74);
	draw_text(font_small, "D-pad up/down: choose     left/right or \xE2\x9C\x95: change     \xE2\x96\xB3  recommended layout     \xE2\x97\x8B  back",
	          90, SCREEN_H - 98, white);
	if (hint && hint[0]) draw_text_fit(font_small, hint, 90, SCREEN_H - 54, SCREEN_W - 180, gold);
	maybe_shot();
	SDL_RenderPresent(ren);
}

/* --- main loop --- */

static TTF_Font* open_font(int size) {
	const char* env_font = getenv("RPGMAKER_FONT");
	TTF_Font* f = TTF_OpenFont(env_font ? env_font : FONT_PATH, size);
	if (!f) f = TTF_OpenFont("/data/homebrew/mkxp-z/font.ttf", size);
	return f;
}

int main(void) {
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) != 0) return 1;
	if (TTF_Init() != 0) return 1;
	SDL_Window* win = SDL_CreateWindow("Tsukuru Player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_W, SCREEN_H,
	                                   SDL_WINDOW_FULLSCREEN);
	if (!win) return 1;
	ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
	if (!ren) return 1;

	font_big = open_font(72);
	font_mid = open_font(40);
	font_small = open_font(30);
	if (!font_big || !font_mid || !font_small) {
		fprintf(stderr, "rpgmaker launcher: cannot open the font %s\n", FONT_PATH);
		return 1;
	}

	SDL_Joystick* joy = SDL_NumJoysticks() > 0 ? SDL_JoystickOpen(0) : NULL;
	key_table_init();
	settings_load();
	find_local_ip();
	scan_games();
	int screen = 0, srow = 0, sscroll = 0; /* 0: game list, 1: settings */
	shot_path = getenv("RPGMAKER_SHOT");
	if (shot_path && getenv("RPGMAKER_SHOT_SETTINGS")) screen = 1;
	if (shot_path && getenv("RPGMAKER_SHOT_ROW")) {
		srow = atoi(getenv("RPGMAKER_SHOT_ROW"));
		if (srow >= SETTINGS_ROWS) srow = SETTINGS_ROWS - 1;
		if (srow >= SETTINGS_VISIBLE) sscroll = srow - SETTINGS_VISIBLE + 1;
	}

	int selected = 0, scroll = 0, dirty = 1, hold = 0, warned = -1, delete_armed = -1;
	Uint32 next_repeat = 0, delete_redraw = 0;
	char delete_name[256] = "";
	char message[700] = "";
	if (game_count > 0 && games[0].missing_rtp[0]) {
		snprintf(message, sizeof message,
		         "! This game needs the RTP \"%s\". Put a folder with that name into a folder called \"rtp\" on your USB stick (or /data/rtp).",
		         games[0].missing_rtp);
	}
	if (game_count > 0 && games[0].protected_files) snprintf(message, sizeof message, "%s", PROTECTED_MESSAGE);
	if (game_count > 0 && games[0].incomplete) snprintf(message, sizeof message, "%s", INCOMPLETE_MESSAGE);
	last_run_message(message, sizeof message);
	int running = 1;

	while (running) {
		int move = 0;
		SDL_Event e;
		while (SDL_PollEvent(&e)) {
			if (e.type == SDL_QUIT) running = 0;
			if (delete_busy) continue; /* buttons do nothing while a game is being deleted */
			if (e.type == SDL_JOYBUTTONDOWN && screen == 1) {
				switch (e.jbutton.button) {
				case 0: /* Cross */
					settings_change(srow, 1);
					dirty = 1;
					break;
				case 14: /* D-pad right */
					settings_change(srow, 1);
					dirty = 1;
					break;
				case 13: /* D-pad left */
					settings_change(srow, -1);
					dirty = 1;
					break;
				case 3: /* Triangle: defaults */
					settings_change(SETTINGS_ROWS - 1, 1);
					dirty = 1;
					break;
				case 9: /* L1 */
					settings_change(srow, -10);
					dirty = 1;
					break;
				case 10: /* R1 */
					settings_change(srow, 10);
					dirty = 1;
					break;
				case 1: /* Circle: back */
					screen = 0;
					dirty = 1;
					break;
				}
				continue;
			}
			if (e.type == SDL_JOYBUTTONDOWN) {
				if (delete_armed >= 0 && e.jbutton.button != 2) {
					delete_armed = -1; /* any other button cancels */
					message[0] = '\0';
					dirty = 1;
				}
				switch (e.jbutton.button) {
				case 2: /* Square: delete a game that is stored on the console */
					if (game_count == 0) break;
					if (!can_delete(&games[selected])) {
						snprintf(message, sizeof message, "Only games stored on the console can be deleted here. Games on a USB stick stay on the stick.");
					} else if (delete_armed != selected) {
						delete_armed = selected;
						snprintf(message, sizeof message, "Delete \"%s\" from the console? Press Square again to delete it, any other button to cancel.",
						         games[selected].name);
					} else {
						snprintf(delete_name, sizeof delete_name, "%s", games[selected].name);
						delete_armed = -1;
						if (delete_start(games[selected].path) == 0) {
							snprintf(message, sizeof message, "Deleting \"%s\" ... counting files", delete_name);
							delete_redraw = SDL_GetTicks();
						} else {
							snprintf(message, sizeof message, "Could not start deleting \"%s\".", delete_name);
						}
					}
					dirty = 1;
					break;
				case 6: /* Options */
					screen = 1;
					dirty = 1;
					break;
				case 0: /* Cross */
					if (game_count > 0) {
						int r = start_game(&games[selected], warned == selected, message, sizeof message);
						warned = (r == 1) ? selected : -1;
						dirty = 1;
					}
					break;
				case 3: /* Triangle */
					find_local_ip();
					scan_games();
					if (selected >= game_count) selected = game_count ? game_count - 1 : 0;
					snprintf(message, sizeof message, "Found %d game%s.", game_count, game_count == 1 ? "" : "s");
					dirty = 1;
					break;
				case 9: move = -(VISIBLE_ROWS - 1); break;  /* L1 */
				case 10: move = VISIBLE_ROWS - 1; break;    /* R1 */
				}
			}
		}

		if (delete_busy) {
			/* progress bar while the deleting thread works; redraw about 8 times a second */
			Uint32 t = SDL_GetTicks();
			int total = SDL_AtomicGet(&del_total), done = SDL_AtomicGet(&del_done);
			if (SDL_AtomicGet(&del_finished)) {
				SDL_WaitThread(del_thread, NULL);
				del_thread = NULL;
				delete_busy = 0;
				int rc = SDL_AtomicGet(&del_result);
				scan_games();
				if (selected >= game_count) selected = game_count ? game_count - 1 : 0;
				if (scroll > selected) scroll = selected;
				snprintf(message, sizeof message, rc == 0 ? "Deleted \"%s\"." : "Could not delete all of \"%s\".", delete_name);
				dirty = 1;
			} else if (t - delete_redraw >= 120) {
				delete_redraw = t;
				if (total > 0) {
					delete_permille = (int)((long long)done * 1000 / total);
					if (delete_permille > 1000) delete_permille = 1000;
					snprintf(message, sizeof message, "Deleting \"%s\" ... %d of %d files (%d%%). Please wait.", delete_name, done,
					         total, delete_permille / 10);
				} else {
					snprintf(message, sizeof message, "Deleting \"%s\" ... counting files", delete_name);
				}
				dirty = 1;
			}
			move = 0;
		}

		/* held direction (D-pad hat/buttons or left stick) with key repeat */
		int dir = 0;
		if (joy) {
			Uint8 hat = SDL_JoystickNumHats(joy) > 0 ? SDL_JoystickGetHat(joy, 0) : 0;
			if ((hat & SDL_HAT_UP) || SDL_JoystickGetButton(joy, 11) || SDL_JoystickGetAxis(joy, 1) < -16000) dir = -1;
			if ((hat & SDL_HAT_DOWN) || SDL_JoystickGetButton(joy, 12) || SDL_JoystickGetAxis(joy, 1) > 16000) dir = 1;
		}
		Uint32 now = SDL_GetTicks();
		if (dir != hold) {
			hold = dir;
			if (dir) {
				move = dir;
				next_repeat = now + 350;
			}
		} else if (dir && now >= next_repeat) {
			move = dir;
			next_repeat = now + 80;
		}

		if (move && screen == 1) {
			srow += move > 0 ? 1 : -1;
			if (srow < 0) srow = 0;
			if (srow >= SETTINGS_ROWS) srow = SETTINGS_ROWS - 1;
			if (srow < sscroll) sscroll = srow;
			if (srow >= sscroll + SETTINGS_VISIBLE) sscroll = srow - SETTINGS_VISIBLE + 1;
			dirty = 1;
			move = 0;
		}

		if (move && game_count > 0 && screen == 0 && !delete_busy) {
			selected += move;
			if (selected < 0) selected = 0;
			if (selected >= game_count) selected = game_count - 1;
			if (selected < scroll) scroll = selected;
			if (selected >= scroll + VISIBLE_ROWS) scroll = selected - VISIBLE_ROWS + 1;
			warned = -1;
			delete_armed = -1;
			message[0] = '\0';
			if (games[selected].missing_rtp[0]) {
				snprintf(message, sizeof message,
				         "! This game needs the RTP \"%s\". Put a folder with that name into a folder called \"rtp\" on your USB stick (or /data/rtp).",
				         games[selected].missing_rtp);
			}
			if (games[selected].protected_files) snprintf(message, sizeof message, "%s", PROTECTED_MESSAGE);
			if (games[selected].incomplete) snprintf(message, sizeof message, "%s", INCOMPLETE_MESSAGE);
			dirty = 1;
		}

		if (dirty) {
			if (screen == 1) draw_settings(srow, sscroll, "Changes are saved as you make them and apply the next time you start a game.");
			else draw_screen(selected, scroll, message);
			dirty = 0;
		}
		SDL_Delay(16);
	}

	TTF_Quit();
	SDL_Quit();
	return 0;
}
