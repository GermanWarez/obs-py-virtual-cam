/*
 * uds.h — UNIX domain socket lifecycle for the obs-py-virtual-cam host.
 *
 * The host owns the socket lifecycle: it derives a well-known path under a
 * per-user 0700 temp directory, unlinks any stale socket, binds a 0600 socket,
 * and listens for a single client at a time. Teardown closes/unlinks the
 * socket and removes the directory if it is empty.
 *
 * Pure C with no libobs dependency so it is unit-testable with plain clang
 * (see tests/test_uds.c).
 */

#ifndef PY_VIRTUAL_CAM_UDS_H
#define PY_VIRTUAL_CAM_UDS_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Derive the default socket path: "$TMPDIR/py-virtual-cam/feed.sock", falling
 * back to "/tmp/py-virtual-cam/feed.sock" when TMPDIR is unset or empty.
 *
 *   out      destination buffer; the NUL-terminated path is written here.
 *   out_size size of the destination buffer in bytes.
 *
 * Returns true on success, false if out is NULL/too small. The resulting path
 * always fits inside a sockaddr_un.sun_path (see UDS_SOCKET_PATH_MAX).
 */
bool uds_derive_socket_path(char *out, size_t out_size);

/*
 * Prepare the listening socket for the given path:
 *   - create the parent directory (mode 0700, owned by the running user),
 *   - unlink any stale socket at the path,
 *   - create an AF_UNIX SOCK_STREAM socket, bind() it, chmod it to 0600,
 *   - listen() with a small backlog.
 *
 * Returns the listening fd on success, or -1 on failure (errno is set).
 */
int uds_setup(const char *path);

/*
 * Tear down the listening socket:
 *   - close listen_fd (if >= 0),
 *   - unlink the socket file,
 *   - rmdir() the parent directory if it is now empty.
 *
 * Safe to call with listen_fd < 0 (only unlink + rmdir are attempted).
 */
void uds_teardown(int listen_fd, const char *path);

/*
 * Maximum socket path length that is guaranteed to fit in sockaddr_un.sun_path
 * across platforms (the smallest common sun_path is 104 bytes on macos/BSD;
 * reserve one byte for the NUL terminator).
 */
#define UDS_SOCKET_PATH_MAX 104

#ifdef __cplusplus
}
#endif

#endif /* PY_VIRTUAL_CAM_UDS_H */
