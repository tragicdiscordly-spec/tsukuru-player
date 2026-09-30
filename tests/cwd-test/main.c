/* Shows what a program launched through websrv sees: working directory, environment and which
 * kinds of file paths work. Run with --pipe; nothing is drawn on the screen. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void try_stat(const char* path) {
	struct stat st;
	errno = 0;
	int r = stat(path, &st);
	printf("  stat(\"%s\") = %d, errno %d (%s)\n", path, r, errno, r ? strerror(errno) : "ok");
}

static void try_open(const char* path) {
	errno = 0;
	int fd = open(path, O_RDONLY);
	printf("  open(\"%s\") = %d, errno %d (%s)\n", path, fd, errno, fd < 0 ? strerror(errno) : "ok");
	if (fd >= 0) close(fd);
}

int main(int argc, char** argv) {
	char buf[1024];
	printf("argc=%d argv[0]=%s\n", argc, argv[0]);
	for (int i = 1; i < argc; i++) printf("argv[%d]=%s\n", i, argv[i]);
	printf("HOME=%s\n", getenv("HOME") ? getenv("HOME") : "(unset)");
	printf("PWD=%s\n", getenv("PWD") ? getenv("PWD") : "(unset)");

	errno = 0;
	char* cwd = getcwd(buf, sizeof buf);
	printf("getcwd() = %s (errno %d: %s)\n", cwd ? cwd : "NULL", errno, strerror(errno));

	printf("absolute paths:\n");
	try_stat("/data/games/rgss-test/Game.ini");
	try_open("/data/games/rgss-test/Game.ini");
	try_stat("/data/games/rgss-test");

	printf("relative paths (as launched):\n");
	try_stat("Game.ini");
	try_open("Game.ini");
	try_stat("./Game.ini");
	try_stat(".");
	try_stat("Data/Scripts.rxdata");

	/* Pass "budget" to also try the system call that websrv itself makes before starting a
	 * homebrew, sys_set_budget(0), which is supposed to allow relative paths. */
	if (argc > 1 && !strcmp(argv[1], "budget")) {
		errno = 0;
		long br = syscall(0x23b, 0);
		printf("syscall(0x23b, 0) = %ld, errno %d (%s)\n", br, errno, br ? strerror(errno) : "ok");
		printf("relative paths after sys_set_budget(0):\n");
		try_stat("Game.ini");
		try_open("Game.ini");
		try_stat("Data/Scripts.rxdata");
	}

	printf("chdir to /data/games/rgss-test:\n");
	errno = 0;
	int r = chdir("/data/games/rgss-test");
	printf("  chdir = %d, errno %d (%s)\n", r, errno, r ? strerror(errno) : "ok");
	errno = 0;
	cwd = getcwd(buf, sizeof buf);
	printf("  getcwd() = %s (errno %d: %s)\n", cwd ? cwd : "NULL", errno, strerror(errno));
	try_stat("Game.ini");
	try_open("Game.ini");
	try_stat("Data/Scripts.rxdata");
	try_stat("mkxp.json");
	return 0;
}
