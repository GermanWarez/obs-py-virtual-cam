/*
 * uds.c — UNIX domain socket lifecycle for the obs-py-virtual-cam host.
 *
 * See uds.h. Pure C (POSIX), no libobs dependency.
 */

#include "uds.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

/* Directory and socket basename placed under the temp directory. */
#define UDS_DIR_NAME "py-virtual-cam"
#define UDS_SOCK_NAME "feed.sock"

bool uds_derive_socket_path(char *out, size_t out_size)
{
	if (!out || out_size == 0) {
		return false;
	}

	const char *tmpdir = getenv("TMPDIR");
	if (!tmpdir || tmpdir[0] == '\0') {
		tmpdir = "/tmp";
	}

	/* Strip a single trailing slash so we don't emit a double slash. */
	size_t tmplen = strlen(tmpdir);
	char base[PATH_MAX];
	if (tmplen >= sizeof(base)) {
		errno = ENAMETOOLONG;
		return false;
	}
	memcpy(base, tmpdir, tmplen + 1);
	if (tmplen > 1 && base[tmplen - 1] == '/') {
		base[tmplen - 1] = '\0';
	}

	int n = snprintf(out, out_size, "%s/%s/%s", base, UDS_DIR_NAME, UDS_SOCK_NAME);
	if (n < 0 || (size_t)n >= out_size || (size_t)n >= UDS_SOCKET_PATH_MAX) {
		errno = ENAMETOOLONG;
		return false;
	}
	return true;
}

/*
 * Copy the parent directory of path into dir (NUL-terminated). Returns true on
 * success, false if path has no '/' or dir is too small.
 */
static bool uds_parent_dir(const char *path, char *dir, size_t dir_size)
{
	const char *slash = strrchr(path, '/');
	if (!slash || slash == path) {
		return false;
	}
	size_t len = (size_t)(slash - path);
	if (len + 1 > dir_size) {
		return false;
	}
	memcpy(dir, path, len);
	dir[len] = '\0';
	return true;
}

int uds_setup(const char *path)
{
	if (!path || path[0] == '\0') {
		errno = EINVAL;
		return -1;
	}

	size_t path_len = strlen(path);
	if (path_len >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
		errno = ENAMETOOLONG;
		return -1;
	}

	/* Create the parent directory, owner-only (0700). */
	char dir[PATH_MAX];
	if (!uds_parent_dir(path, dir, sizeof(dir))) {
		errno = EINVAL;
		return -1;
	}
	if (mkdir(dir, 0700) != 0 && errno != EEXIST) {
		return -1;
	}
	/*
	 * Enforce 0700 even if the directory already existed (defense in depth;
	 * it must be owner-only regardless of a stale umask-affected creation).
	 */
	if (chmod(dir, 0700) != 0) {
		return -1;
	}

	/* Remove any stale socket left over from a previous run. */
	if (unlink(path) != 0 && errno != ENOENT) {
		return -1;
	}

	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	memcpy(addr.sun_path, path, path_len + 1);

	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		int saved = errno;
		close(fd);
		errno = saved;
		return -1;
	}

	/* Socket file mode 0600 (defense in depth). */
	if (chmod(path, 0600) != 0) {
		int saved = errno;
		close(fd);
		unlink(path);
		errno = saved;
		return -1;
	}

	if (listen(fd, 1) != 0) {
		int saved = errno;
		close(fd);
		unlink(path);
		errno = saved;
		return -1;
	}

	return fd;
}

void uds_teardown(int listen_fd, const char *path)
{
	if (listen_fd >= 0) {
		close(listen_fd);
	}

	if (!path || path[0] == '\0') {
		return;
	}

	unlink(path);

	char dir[PATH_MAX];
	if (uds_parent_dir(path, dir, sizeof(dir))) {
		/* rmdir() only succeeds (and only should) if the dir is empty. */
		rmdir(dir);
	}
}
