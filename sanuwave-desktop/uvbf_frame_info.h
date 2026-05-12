// uvbf_frame_info.h
//
// Wire-protocol metadata for UVBF frame_transfer payloads. Carried alongside
// the binary DNG buffer in ServerConnection::uvbfFrameTransferComplete so
// receivers can write a proper DNG without re-parsing the JSON envelope.
//
// Extracted from server_connection.h so consumers (e.g. UVBFVBlankDialog)
// can depend on this type without pulling in all of ServerConnection.

#ifndef UVBF_FRAME_INFO_H
#define UVBF_FRAME_INFO_H

#include <cstdint>
#include <QString>
#include "raw_bayer_decoding.h"   // sanuwave::RawImageInfo

struct UVBFFrameInfo
{
    QString role;
    QString sessionId;
    QString camera;
    uint64_t captureTimestamp_ms = 0;
    uint64_t ledOnTimestamp_ms = 0;
    uint64_t ledOffTimestamp_ms = 0;
    sanuwave::RawImageInfo imageInfo; // width, height, bitDepth, storageBits,
                                      // blackLevel, pattern, exposureUs

    // Inter-frame motion measurements from UVBF burst phase correlation.
    // Populated only for illum frames at illum-sequence index k >= 2,
    // and only when the server-side motion check was enabled for this
    // capture. valid==false means no motion sub-object was emitted by
    // the server for this frame.
    //
    // Two measurements per illum frame: rolling "prev" (since the previous
    // illum frame) and fixed "anchor" (since illum1). For the second illum
    // frame the prev_* and anchor_* pairs are identical (illum1 IS the
    // previous frame); the schema is reported uniformly for client simplicity.
    struct Motion {
        bool   valid             = false;
        double prevTransPx       = 0.0;   // motion since previous illum frame
        double prevConfidence    = 0.0;
        double anchorTransPx     = 0.0;   // motion since illum1 (the anchor)
        double anchorConfidence  = 0.0;
    } motion;
};

#endif // UVBF_FRAME_INFO_H
