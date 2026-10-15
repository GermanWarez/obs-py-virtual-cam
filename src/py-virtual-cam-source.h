/*
 * py-virtual-cam-source.h — the obs-py-virtual-cam OBS source.
 *
 * Exposes the obs_source_info describing the async-video source. The source
 * listens on a UNIX domain socket for raw headerless BGRA frames (one client
 * at a time) and outputs them via obs_source_output_video. Until the first
 * real frame arrives it renders a placeholder: the Python sample source text
 * matching the selected resolution, with a blinking cursor.
 */

#ifndef PY_VIRTUAL_CAM_SOURCE_H
#define PY_VIRTUAL_CAM_SOURCE_H

#include <obs-module.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The source definition registered by obs_module_load(). */
extern struct obs_source_info py_virtual_cam_feed_info;

#ifdef __cplusplus
}
#endif

#endif /* PY_VIRTUAL_CAM_SOURCE_H */
