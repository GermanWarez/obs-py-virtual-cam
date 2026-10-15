/*
 * test_uds.c — plain-clang unit test for the UDS lifecycle.
 *
 * Build & run (no libobs):
 *   clang -std=c17 -o /tmp/tu tests/test_uds.c src/uds.c && /tmp/tu
 *
 * Verifies that:
 *   - uds_setup() creates the parent directory mode 0700,
 *   - the bound socket file is mode 0600,
 *   - uds_teardown() unlinks the socket and removes the (now empty) directory,
 *   - uds_derive_socket_path() honours TMPDIR and falls back to /tmp.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>

#include "../src/uds.h"

static int failures = 0;

#define CHECK(cond, msg)                                        \
	do {                                                    \
		if (!(cond)) {                                  \
			fprintf(stderr, "FAIL: %s\n", (msg));   \
			failures++;                             \
		}                                               \
	} while (0)

/* Return the lowest 12 permission bits of a path, or -1 on stat failure. */
static int mode_of(const char *path)
{
	struct stat st;
	if (stat(path, &st) != 0) {
		return -1;
	}
	return (int)(st.st_mode & 07777);
}

static bool path_exists(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0;
}

int main(void)
{
	/*
	 * Use a private TMPDIR so the test is hermetic and does not collide
	 * with a running plugin's real socket directory.
	 */
	char tmpl[] = "/tmp/uds_test_XXXXXX";
	char *base = mkdtemp(tmpl);
	CHECK(base != NULL, "mkdtemp for a private TMPDIR");
	if (!base) {
		return 1;
	}
	setenv("TMPDIR", base, 1);

	char path[UDS_SOCKET_PATH_MAX];
	CHECK(uds_derive_socket_path(path, sizeof(path)), "derive socket path");

	/* Derived path must live under $TMPDIR/py-virtual-cam/feed.sock. */
	char expected[UDS_SOCKET_PATH_MAX];
	snprintf(expected, sizeof(expected), "%s/py-virtual-cam/feed.sock", base);
	CHECK(strcmp(path, expected) == 0, "derived path honours TMPDIR");

	/* Compute the parent directory for independent checks. */
	char dir[256];
	snprintf(dir, sizeof(dir), "%s/py-virtual-cam", base);

	int fd = uds_setup(path);
	CHECK(fd >= 0, "uds_setup returns a valid listen fd");

	CHECK(mode_of(dir) == 0700, "parent directory is mode 0700");
	CHECK(mode_of(path) == 0600, "socket file is mode 0600");

	/* A second setup must succeed by unlinking the stale socket. */
	int fd2 = uds_setup(path);
	CHECK(fd2 >= 0, "uds_setup succeeds against a stale socket");
	CHECK(mode_of(path) == 0600, "re-created socket is still mode 0600");
	if (fd >= 0) {
		close(fd);
	}

	uds_teardown(fd2, path);
	CHECK(!path_exists(path), "teardown unlinks the socket file");
	CHECK(!path_exists(dir), "teardown removes the empty directory");

	/* TMPDIR fallback: unset -> /tmp. */
	unsetenv("TMPDIR");
	char fallback[UDS_SOCKET_PATH_MAX];
	CHECK(uds_derive_socket_path(fallback, sizeof(fallback)), "derive with TMPDIR unset");
	CHECK(strcmp(fallback, "/tmp/py-virtual-cam/feed.sock") == 0, "fallback path is /tmp/py-virtual-cam/feed.sock");

	/* Clean up the private temp root (dir already removed by teardown). */
	rmdir(base);

	if (failures == 0) {
		printf("ok: all uds assertions passed\n");
		return 0;
	}
	fprintf(stderr, "%d assertion(s) failed\n", failures);
	return 1;
}
