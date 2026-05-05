// src/rgb_stream_worker.h
// RGB camera stream worker thread function.

#ifndef RGB_STREAM_WORKER_H
#define RGB_STREAM_WORKER_H

#include "stream_context.h"

class CameraBase;

namespace sanuwave
{

// Runs at the camera's native frame rate (IMX708 or IMX219).
void rgbStreamWorker(CameraBase* camera, StreamContext ctx);

} // namespace sanuwave

#endif // RGB_STREAM_WORKER_H
