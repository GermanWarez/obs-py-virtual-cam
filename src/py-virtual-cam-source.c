/*
 * py-virtual-cam-source.c — the obs-py-virtual-cam OBS source.
 *
 * See py-virtual-cam-source.h. Implements the full async-video source:
 *   - properties (resolution dropdown + editable socket path) and defaults,
 *   - a reader thread that accepts one client at a time and streams raw
 *     headerless BGRA frames (exactly width*height*4 bytes each) out via
 *     obs_source_output_video,
 *   - a placeholder thread that renders the matching Python sample source with
 *     a blinking cursor at ~12fps until the first real frame arrives.
 */

#include "py-virtual-cam-source.h"

#include <obs-module.h>
#include <plugin-support.h>
#include <util/platform.h>
#include <util/bmem.h>

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>

#include "generated/sample_sources.h"
#include "text_render.h"
#include "uds.h"

/* Settings keys. */
#define SETTING_RESOLUTION "resolution"
#define SETTING_SOCKET_PATH "socket_path"

/* Resolution dropdown values: the int stored is the frame width. */
#define RES_480P_W 854
#define RES_480P_H 480
#define RES_720P_W 1280
#define RES_720P_H 720
#define RES_1080P_W 1920
#define RES_1080P_H 1080

/* Default resolution is 720p. */
#define DEFAULT_RES_W RES_720P_W
#define DEFAULT_RES_H RES_720P_H

/* Placeholder animation timing. */
#define PLACEHOLDER_FRAME_NS (83 * 1000 * 1000ULL) /* ~12 fps */
#define CURSOR_BLINK_NS (500 * 1000 * 1000ULL)     /* ~500ms toggle */

/* How often blocking poll() loops wake to re-check the stop flag. */
#define POLL_TICK_MS 100

struct py_virtual_cam_feed {
	obs_source_t *source;

	uint32_t width;
	uint32_t height;
	char socket_path[UDS_SOCKET_PATH_MAX];

	int listen_fd;

	pthread_t reader_thread;
	pthread_t placeholder_thread;
	bool reader_started;
	bool placeholder_started;

	/* Reader frame buffer, width*height*4 bytes (BGRA). */
	uint8_t *frame_buf;
	size_t frame_size;

	/* Placeholder render buffer, width*height*4 bytes. */
	uint8_t *placeholder_buf;

	volatile bool stop;
	volatile bool first_real_frame;
};

/* Map a frame width to the matching Python sample source text. */
static const char *sample_source_for(uint32_t width, uint32_t height)
{
	if (width == RES_1080P_W && height == RES_1080P_H) {
		return SAMPLE_SRC_1080P;
	}
	if (width == RES_480P_W && height == RES_480P_H) {
		return SAMPLE_SRC_480P;
	}
	return SAMPLE_SRC_720P;
}

/* Resolve a stored resolution int (= width) to width/height. */
static void resolve_resolution(long long value, uint32_t *width, uint32_t *height)
{
	switch (value) {
	case RES_480P_W:
		*width = RES_480P_W;
		*height = RES_480P_H;
		return;
	case RES_1080P_W:
		*width = RES_1080P_W;
		*height = RES_1080P_H;
		return;
	case RES_720P_W:
	default:
		*width = RES_720P_W;
		*height = RES_720P_H;
		return;
	}
}

static const char *py_virtual_cam_feed_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("PyVirtualCamFeed");
}

static void py_virtual_cam_feed_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_RESOLUTION, DEFAULT_RES_W);

	char path[UDS_SOCKET_PATH_MAX];
	if (uds_derive_socket_path(path, sizeof(path))) {
		obs_data_set_default_string(settings, SETTING_SOCKET_PATH, path);
	}
}

static obs_properties_t *py_virtual_cam_feed_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();

	obs_property_t *res = obs_properties_add_list(props, SETTING_RESOLUTION, obs_module_text("Resolution"),
						      OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(res, "854x480 (480p)", RES_480P_W);
	obs_property_list_add_int(res, "1280x720 (720p)", RES_720P_W);
	obs_property_list_add_int(res, "1920x1080 (1080p)", RES_1080P_W);

	/* Read-only: shows the derived socket path for reference/debugging.
	 * The path is not user-editable — the client derives the same path, so
	 * an editable field would only invite silent plugin/client mismatches. */
	obs_properties_add_text(props, SETTING_SOCKET_PATH, obs_module_text("SocketPath"), OBS_TEXT_INFO);

	return props;
}

static uint32_t py_virtual_cam_feed_get_width(void *data)
{
	struct py_virtual_cam_feed *ctx = data;
	return ctx ? ctx->width : DEFAULT_RES_W;
}

static uint32_t py_virtual_cam_feed_get_height(void *data)
{
	struct py_virtual_cam_feed *ctx = data;
	return ctx ? ctx->height : DEFAULT_RES_H;
}

/*
 * Read exactly len bytes into buf, looping over partial recv()s. Wakes every
 * POLL_TICK_MS to honour the stop flag. Returns:
 *    1  full buffer read,
 *    0  peer closed (EOF) or stop requested,
 *   -1  error.
 */
static int recv_exact(struct py_virtual_cam_feed *ctx, int fd, uint8_t *buf, size_t len)
{
	size_t got = 0;
	while (got < len && !ctx->stop) {
		struct pollfd pfd = {.fd = fd, .events = POLLIN};
		int pr = poll(&pfd, 1, POLL_TICK_MS);
		if (pr < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (pr == 0) {
			continue; /* timeout: re-check stop */
		}
		ssize_t n = recv(fd, buf + got, len - got, 0);
		if (n == 0) {
			return 0; /* EOF */
		}
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			return -1;
		}
		got += (size_t)n;
	}
	return ctx->stop ? 0 : 1;
}

/*
 * Accept one client, honouring the stop flag. Returns the client fd, or -1 if
 * stop was requested or accept failed.
 */
static int accept_client(struct py_virtual_cam_feed *ctx)
{
	while (!ctx->stop) {
		struct pollfd pfd = {.fd = ctx->listen_fd, .events = POLLIN};
		int pr = poll(&pfd, 1, POLL_TICK_MS);
		if (pr < 0) {
			if (errno == EINTR) {
				continue;
			}
			return -1;
		}
		if (pr == 0) {
			continue; /* timeout: re-check stop */
		}
		int client = accept(ctx->listen_fd, NULL, NULL);
		if (client < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			return -1;
		}
		return client;
	}
	return -1;
}

static void *reader_thread_fn(void *arg)
{
	struct py_virtual_cam_feed *ctx = arg;

	while (!ctx->stop) {
		int client = accept_client(ctx);
		if (client < 0) {
			break; /* stop requested or fatal accept error */
		}
		obs_log(LOG_INFO, "py-virtual-cam: client connected");

		/* Stream frames until EOF/error, then drop and re-accept. */
		while (!ctx->stop) {
			int r = recv_exact(ctx, client, ctx->frame_buf, ctx->frame_size);
			if (r != 1) {
				/* EOF, stop, or error: discard partial frame. */
				break;
			}

			struct obs_source_frame frame;
			memset(&frame, 0, sizeof(frame));
			frame.data[0] = ctx->frame_buf;
			frame.linesize[0] = ctx->width * 4;
			frame.width = ctx->width;
			frame.height = ctx->height;
			frame.format = VIDEO_FORMAT_BGRA;
			frame.full_range = true;
			frame.timestamp = os_gettime_ns();

			obs_source_output_video(ctx->source, &frame);
			ctx->first_real_frame = true;
		}

		close(client);
		obs_log(LOG_INFO, "py-virtual-cam: client disconnected");
	}

	return NULL;
}

static void *placeholder_thread_fn(void *arg)
{
	struct py_virtual_cam_feed *ctx = arg;

	uint64_t start = os_gettime_ns();

	while (!ctx->stop && !ctx->first_real_frame) {
		uint64_t now = os_gettime_ns();
		bool cursor_visible = ((now - start) / CURSOR_BLINK_NS) % 2 == 0;

		const char *text = sample_source_for(ctx->width, ctx->height);
		text_render_bgra(ctx->placeholder_buf, (int)ctx->width, (int)ctx->height, text, cursor_visible);

		struct obs_source_frame frame;
		memset(&frame, 0, sizeof(frame));
		frame.data[0] = ctx->placeholder_buf;
		frame.linesize[0] = ctx->width * 4;
		frame.width = ctx->width;
		frame.height = ctx->height;
		frame.format = VIDEO_FORMAT_BGRA;
		frame.full_range = true;
		frame.timestamp = os_gettime_ns();

		/* The reader may have set first_real_frame between the check and
		 * here; one extra placeholder frame is harmless and ordering is
		 * preserved by OBS timestamps. */
		if (!ctx->first_real_frame) {
			obs_source_output_video(ctx->source, &frame);
		}

		os_sleep_ms((uint32_t)(PLACEHOLDER_FRAME_NS / 1000000ULL));
	}

	return NULL;
}

/* Allocate the reader + placeholder buffers for the current resolution. */
static bool alloc_buffers(struct py_virtual_cam_feed *ctx)
{
	size_t size = (size_t)ctx->width * ctx->height * 4;
	ctx->frame_size = size;
	ctx->frame_buf = brealloc(ctx->frame_buf, size);
	ctx->placeholder_buf = brealloc(ctx->placeholder_buf, size);
	return ctx->frame_buf != NULL && ctx->placeholder_buf != NULL;
}

/* Start the UDS listener + both threads. Returns true on success. */
static bool start_feed(struct py_virtual_cam_feed *ctx)
{
	ctx->stop = false;
	ctx->first_real_frame = false;

	if (!alloc_buffers(ctx)) {
		obs_log(LOG_ERROR, "py-virtual-cam: failed to allocate frame buffers");
		return false;
	}

	ctx->listen_fd = uds_setup(ctx->socket_path);
	if (ctx->listen_fd < 0) {
		obs_log(LOG_ERROR, "py-virtual-cam: uds_setup('%s') failed: %s", ctx->socket_path, strerror(errno));
		return false;
	}

	if (pthread_create(&ctx->reader_thread, NULL, reader_thread_fn, ctx) != 0) {
		obs_log(LOG_ERROR, "py-virtual-cam: failed to start reader thread");
		uds_teardown(ctx->listen_fd, ctx->socket_path);
		ctx->listen_fd = -1;
		return false;
	}
	ctx->reader_started = true;

	if (pthread_create(&ctx->placeholder_thread, NULL, placeholder_thread_fn, ctx) != 0) {
		obs_log(LOG_ERROR, "py-virtual-cam: failed to start placeholder thread");
		/* Reader is running; stop it before bailing. */
		ctx->stop = true;
		pthread_join(ctx->reader_thread, NULL);
		ctx->reader_started = false;
		uds_teardown(ctx->listen_fd, ctx->socket_path);
		ctx->listen_fd = -1;
		return false;
	}
	ctx->placeholder_started = true;

	obs_log(LOG_INFO, "py-virtual-cam: feed started (%ux%u) on %s", ctx->width, ctx->height, ctx->socket_path);
	return true;
}

/* Stop both threads and tear down the socket. Safe to call when not started. */
static void stop_feed(struct py_virtual_cam_feed *ctx)
{
	ctx->stop = true;

	if (ctx->reader_started) {
		pthread_join(ctx->reader_thread, NULL);
		ctx->reader_started = false;
	}
	if (ctx->placeholder_started) {
		pthread_join(ctx->placeholder_thread, NULL);
		ctx->placeholder_started = false;
	}

	if (ctx->listen_fd >= 0) {
		uds_teardown(ctx->listen_fd, ctx->socket_path);
		ctx->listen_fd = -1;
	}
}

/* Read settings into the context. Returns true if resolution or path changed. */
static bool apply_settings(struct py_virtual_cam_feed *ctx, obs_data_t *settings)
{
	uint32_t new_w = DEFAULT_RES_W;
	uint32_t new_h = DEFAULT_RES_H;
	resolve_resolution(obs_data_get_int(settings, SETTING_RESOLUTION), &new_w, &new_h);

	/* The socket path is always derived (the UI field is read-only). Keep the
	 * setting in sync so the info field displays the current path. */
	char new_path[UDS_SOCKET_PATH_MAX];
	if (!uds_derive_socket_path(new_path, sizeof(new_path))) {
		new_path[0] = '\0';
	}
	obs_data_set_string(settings, SETTING_SOCKET_PATH, new_path);

	bool changed = (new_w != ctx->width) || (new_h != ctx->height) || (strcmp(new_path, ctx->socket_path) != 0);

	ctx->width = new_w;
	ctx->height = new_h;
	snprintf(ctx->socket_path, sizeof(ctx->socket_path), "%s", new_path);

	return changed;
}

static void py_virtual_cam_feed_update(void *data, obs_data_t *settings)
{
	struct py_virtual_cam_feed *ctx = data;
	if (!ctx) {
		return;
	}

	bool changed = apply_settings(ctx, settings);
	if (!changed) {
		return;
	}

	/* Resolution or path changed: tear down and re-set-up cleanly. */
	stop_feed(ctx);
	start_feed(ctx);
}

static void *py_virtual_cam_feed_create(obs_data_t *settings, obs_source_t *source)
{
	struct py_virtual_cam_feed *ctx = bzalloc(sizeof(struct py_virtual_cam_feed));
	ctx->source = source;
	ctx->listen_fd = -1;

	apply_settings(ctx, settings);
	start_feed(ctx);

	obs_log(LOG_INFO, "py-virtual-cam: source created");
	return ctx;
}

static void py_virtual_cam_feed_destroy(void *data)
{
	struct py_virtual_cam_feed *ctx = data;
	if (!ctx) {
		return;
	}

	/*
	 * Stop and join both threads BEFORE freeing anything. After stop_feed
	 * returns no thread is running, so there is no obs_source_output_video
	 * in flight once destroy returns.
	 */
	stop_feed(ctx);

	bfree(ctx->frame_buf);
	bfree(ctx->placeholder_buf);
	bfree(ctx);

	obs_log(LOG_INFO, "py-virtual-cam: source destroyed");
}

struct obs_source_info py_virtual_cam_feed_info = {
	.id = "py_virtual_cam_feed",
	.type = OBS_SOURCE_TYPE_INPUT,
	.output_flags = OBS_SOURCE_ASYNC_VIDEO,
	.icon_type = OBS_ICON_TYPE_CAMERA,
	.get_name = py_virtual_cam_feed_get_name,
	.create = py_virtual_cam_feed_create,
	.destroy = py_virtual_cam_feed_destroy,
	.update = py_virtual_cam_feed_update,
	.get_defaults = py_virtual_cam_feed_defaults,
	.get_properties = py_virtual_cam_feed_properties,
	.get_width = py_virtual_cam_feed_get_width,
	.get_height = py_virtual_cam_feed_get_height,
};
