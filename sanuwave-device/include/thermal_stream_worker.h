// src/thermal_stream_worker.h
// Thermal camera stream worker thread function.

#ifndef THERMAL_STREAM_WORKER_H
#define THERMAL_STREAM_WORKER_H

#include "stream_context.h"

class ThermalCamera;

namespace sanuwave
{

// Runs at the Lepton's native rate (~9fps).
// captureScale controls upscaling from native 160x120.
void thermalStreamWorker(ThermalCamera* camera, StreamContext ctx, int captureScale = 1);

} // namespace sanuwave

#endif // THERMAL_STREAM_WORKER_H
