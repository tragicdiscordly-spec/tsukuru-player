/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* Relative path shim for PS5 homebrew.
 *
 * On PS5 firmware 13.60 a program started through the websrv launcher can only use absolute file
 * paths: open("Game.ini"), stat("Data/x") and so on fail with EINVAL, even after chdir() (getcwd()
 * and chdir() with absolute paths work fine, and calling sys_set_budget(0) does not help). Most
 * software is written with relative paths, so wrap the libc functions that take a path: when the
 * path is relative, prefix it with the current directory first.
 *
 * Link this file into the program and add the matching -Wl,--wrap=... options (see WRAPPED below,
 * and scripts/build-mkxp.sh for an example). Functions called from inside libc itself, or from
 * shared libraries loaded with dlopen, are not affected.
 *
 * It can also make file names case-insensitive (PS5PATH_CASE_INSENSITIVE=1), see below.
 *
 * Copyright (C) 2026 the easyrpg-ps5 authors. GPLv3 or later.
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif

#define SHIM_PATH_MAX 4096

/* Returns `path` unchanged if it is NULL, empty or absolute; otherwise "<cwd>/<path>" in `buf`. */
static const char* absolute(const char* path, char* buf) {
	if (!path || path[0] == '/' || path[0] == '\0') {
		return path;
	}
	if (!getcwd(buf, SHIM_PATH_MAX / 2)) {
		return path;
	}
	size_t len = strlen(buf);
	if (len == 0 || buf[len - 1] != '/') {
		buf[len++] = '/';
	}
	strncpy(buf + len, path, SHIM_PATH_MAX - len - 1);
	buf[SHIM_PATH_MAX - 1] = '\0';
	return buf;
}

/* --- case-insensitive fallback ---------------------------------------------------------------------
 *
 * Games made on Windows often ask for "IconSet.png" while the file is called "iconSet.png": Windows does not
 * care, the console's file system does. When PS5PATH_CASE_INSENSITIVE=1 is set in the environment, an open,
 * stat, access or opendir that fails with ENOENT is retried once with every path component that does not
 * exist as spelled replaced by the directory entry that matches it ignoring case. Nothing changes for names
 * that exist as spelled, and files that really do not exist stay missing. Corrections that were found are
 * remembered, so a directory is only scanned once per name. */
DIR* __real_opendir(const char* path);
int __real_lstat(const char* path, struct stat* st);

static int ci_enabled = -1;
static int ci_on(void) {
	if (ci_enabled < 0) {
		const char* e = getenv("PS5PATH_CASE_INSENSITIVE");
		ci_enabled = (e && *e && *e != '0') ? 1 : 0;
	}
	return ci_enabled;
}

#define CI_CACHE_SIZE 1024
static struct { char* from; char* to; } ci_cache[CI_CACHE_SIZE];
static pthread_mutex_t ci_lock = PTHREAD_MUTEX_INITIALIZER;

static unsigned ci_hash(const char* s) {
	unsigned h = 2166136261u;
	while (*s) {
		h = (h ^ (unsigned char)*s++) * 16777619u;
	}
	return h % CI_CACHE_SIZE;
}

/* Finds the entry of `dir` (absolute, "" for the root) that equals `name` ignoring case. */
static int ci_find(const char* dir, const char* name, char* out, size_t out_size) {
	DIR* d = __real_opendir(dir[0] ? dir : "/");
	if (!d) {
		return 0;
	}
	int found = 0;
	struct dirent* e;
	while ((e = readdir(d)) != NULL) {
		if (strcasecmp(e->d_name, name) == 0) {
			strncpy(out, e->d_name, out_size - 1);
			out[out_size - 1] = '\0';
			found = 1;
			break;
		}
	}
	closedir(d);
	return found;
}

/* Returns `path` itself when it needs no correction (or the feature is off), else the corrected path in `buf`.
 * Components after the first one that cannot be found are kept as they are, so creating a new file in an
 * existing directory keeps the requested file name. */
static const char* fix_case(const char* path, char* buf) {
	if (!path || path[0] != '/' || !ci_on()) {
		return path;
	}
	unsigned slot = ci_hash(path);
	pthread_mutex_lock(&ci_lock);
	if (ci_cache[slot].from && strcmp(ci_cache[slot].from, path) == 0) {
		strncpy(buf, ci_cache[slot].to, SHIM_PATH_MAX - 1);
		buf[SHIM_PATH_MAX - 1] = '\0';
		pthread_mutex_unlock(&ci_lock);
		return buf;
	}
	pthread_mutex_unlock(&ci_lock);

	size_t len = 0;
	int changed = 0, missing = 0;
	const char* p = path;
	buf[0] = '\0';
	while (*p) {
		while (*p == '/') {
			p++;
		}
		if (!*p) {
			break;
		}
		const char* end = strchr(p, '/');
		size_t clen = end ? (size_t)(end - p) : strlen(p);
		char comp[256];
		if (clen >= sizeof(comp) || len + 1 + clen + 1 >= SHIM_PATH_MAX) {
			return path;
		}
		memcpy(comp, p, clen);
		comp[clen] = '\0';
		if (!missing) {
			struct stat st;
			buf[len] = '/';
			memcpy(buf + len + 1, comp, clen + 1);
			if (__real_lstat(buf, &st) != 0) {
				char found[256];
				buf[len] = '\0';
				if (ci_find(buf, comp, found, sizeof(found))) {
					changed = 1;
					clen = strlen(found);
					memcpy(comp, found, clen + 1);
				} else {
					missing = 1;
				}
			}
		}
		buf[len] = '/';
		memcpy(buf + len + 1, comp, clen + 1);
		len += 1 + clen;
		p += end ? (size_t)(end - p) : strlen(p);
	}
	if (!changed) {
		return path;
	}
	pthread_mutex_lock(&ci_lock);
	free(ci_cache[slot].from);
	free(ci_cache[slot].to);
	ci_cache[slot].from = strdup(path);
	ci_cache[slot].to = strdup(buf);
	pthread_mutex_unlock(&ci_lock);
	return buf;
}

#define ABS(p) char p##_buf[SHIM_PATH_MAX]; p = absolute(p, p##_buf)
/* Same, for the *at() functions: only relative to the current directory when dirfd == AT_FDCWD. */
#define ABS_AT(fd, p) char p##_buf[SHIM_PATH_MAX]; if ((fd) == AT_FDCWD) p = absolute(p, p##_buf)

/* --- open family --- */
int __real_open(const char* path, int flags, ...);
int __wrap_open(const char* path, int flags, ...) {
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	ABS(path);
	int fd = __real_open(path, flags, mode);
	if (fd < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			fd = __real_open(fixed, flags, mode);
		}
	}
	return fd;
}

int __real_openat(int fd, const char* path, int flags, ...);
int __wrap_openat(int fd, const char* path, int flags, ...) {
	mode_t mode = 0;
	if (flags & O_CREAT) {
		va_list ap;
		va_start(ap, flags);
		mode = (mode_t)va_arg(ap, int);
		va_end(ap);
	}
	ABS_AT(fd, path);
	int r = __real_openat(fd, path, flags, mode);
	if (r < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_openat(fd, fixed, flags, mode);
		}
	}
	return r;
}

FILE* __real_fopen(const char* path, const char* mode);
FILE* __wrap_fopen(const char* path, const char* mode) {
	ABS(path);
	FILE* f = __real_fopen(path, mode);
	if (!f && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			f = __real_fopen(fixed, mode);
		}
	}
	return f;
}

FILE* __real_freopen(const char* path, const char* mode, FILE* stream);
FILE* __wrap_freopen(const char* path, const char* mode, FILE* stream) {
	ABS(path);
	return __real_freopen(path, mode, stream);
}

/* --- stat family --- */
int __real_stat(const char* path, struct stat* st);
int __wrap_stat(const char* path, struct stat* st) {
	ABS(path);
	int r = __real_stat(path, st);
	if (r < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_stat(fixed, st);
		}
	}
	return r;
}

int __real_lstat(const char* path, struct stat* st);
int __wrap_lstat(const char* path, struct stat* st) {
	ABS(path);
	int r = __real_lstat(path, st);
	if (r < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_lstat(fixed, st);
		}
	}
	return r;
}

int __real_fstatat(int fd, const char* path, struct stat* st, int flag);
int __wrap_fstatat(int fd, const char* path, struct stat* st, int flag) {
	ABS_AT(fd, path);
	int r = __real_fstatat(fd, path, st, flag);
	if (r < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_fstatat(fd, fixed, st, flag);
		}
	}
	return r;
}

int __real_access(const char* path, int mode);
int __wrap_access(const char* path, int mode) {
	ABS(path);
	int r = __real_access(path, mode);
	if (r < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_access(fixed, mode);
		}
	}
	return r;
}

int __real_faccessat(int fd, const char* path, int mode, int flag);
int __wrap_faccessat(int fd, const char* path, int mode, int flag) {
	ABS_AT(fd, path);
	int r = __real_faccessat(fd, path, mode, flag);
	if (r < 0 && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_faccessat(fd, fixed, mode, flag);
		}
	}
	return r;
}

/* --- directories --- */
DIR* __wrap_opendir(const char* path) {
	ABS(path);
	DIR* d = __real_opendir(path);
	if (!d && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			d = __real_opendir(fixed);
		}
	}
	return d;
}

int __real_mkdir(const char* path, mode_t mode);
int __wrap_mkdir(const char* path, mode_t mode) {
	ABS(path);
	return __real_mkdir(path, mode);
}

int __real_mkdirat(int fd, const char* path, mode_t mode);
int __wrap_mkdirat(int fd, const char* path, mode_t mode) {
	ABS_AT(fd, path);
	return __real_mkdirat(fd, path, mode);
}

int __real_rmdir(const char* path);
int __wrap_rmdir(const char* path) {
	ABS(path);
	return __real_rmdir(path);
}

int __real_chdir(const char* path);
int __wrap_chdir(const char* path) {
	ABS(path);
	return __real_chdir(path);
}

/* --- removing, renaming, links --- */
int __real_unlink(const char* path);
int __wrap_unlink(const char* path) {
	ABS(path);
	return __real_unlink(path);
}

int __real_unlinkat(int fd, const char* path, int flag);
int __wrap_unlinkat(int fd, const char* path, int flag) {
	ABS_AT(fd, path);
	return __real_unlinkat(fd, path, flag);
}

int __real_rename(const char* from, const char* to);
int __wrap_rename(const char* from, const char* to) {
	ABS(from);
	ABS(to);
	return __real_rename(from, to);
}

ssize_t __real_readlink(const char* path, char* buf, size_t size);
ssize_t __wrap_readlink(const char* path, char* buf, size_t size) {
	ABS(path);
	return __real_readlink(path, buf, size);
}

char* __real_realpath(const char* path, char* resolved);
char* __wrap_realpath(const char* path, char* resolved) {
	ABS(path);
	char* r = __real_realpath(path, resolved);
	if (!r && errno == ENOENT) {
		char ci[SHIM_PATH_MAX];
		const char* fixed = fix_case(path, ci);
		if (fixed != path) {
			r = __real_realpath(fixed, resolved);
		}
	}
	return r;
}

int __real_symlink(const char* target, const char* path);
int __wrap_symlink(const char* target, const char* path) {
	ABS(path);
	return __real_symlink(target, path);
}

/* --- attributes --- */
int __real_chmod(const char* path, mode_t mode);
int __wrap_chmod(const char* path, mode_t mode) {
	ABS(path);
	return __real_chmod(path, mode);
}

int __real_truncate(const char* path, off_t length);
int __wrap_truncate(const char* path, off_t length) {
	ABS(path);
	return __real_truncate(path, length);
}

int __real_utimes(const char* path, const struct timeval* times);
int __wrap_utimes(const char* path, const struct timeval* times) {
	ABS(path);
	return __real_utimes(path, times);
}
