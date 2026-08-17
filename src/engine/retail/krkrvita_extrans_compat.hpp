#pragma once

// Static-backend replacement for SamplePlugin's generated tp_stub.h. The
// transition sources use Kirikiri's public plugin ABI types, while this build
// can call the same engine functions directly instead of through a Windows DLL
// import table.
#include "tjsCommHead.h"
#include "TransIntf.h"
#include "transhandler.h"
#include "RenderManager.h"
#include "tvpgl.h"
#include "DebugIntf.h"
#include "DetectCPU.h"
#include "MsgIntf.h"

inline tjs_error krkrvita_extrans_scanline_for_read(
    iTVPScanLineProvider* provider, tjs_int line, const void** output) {
    if (!provider || !output) return TJS_E_FAIL;
    iTVPTexture2D* texture = provider->GetTexture();
    if (!texture) return TJS_E_FAIL;
    *output = texture->GetScanLineForRead(static_cast<tjs_uint>(line));
    return *output ? TJS_S_OK : TJS_E_FAIL;
}

inline tjs_error krkrvita_extrans_scanline_for_write(
    iTVPScanLineProvider* provider, tjs_int line, void** output) {
    if (!provider || !output) return TJS_E_FAIL;
    iTVPTexture2D* texture = provider->GetTexture();
    if (!texture) return TJS_E_FAIL;
    *output = texture->GetScanLineForWrite(static_cast<tjs_uint>(line));
    return *output ? TJS_S_OK : TJS_E_FAIL;
}

inline tjs_error krkrvita_extrans_pitch(iTVPScanLineProvider* provider,
                                        tjs_int* output) {
    if (!provider || !output) return TJS_E_FAIL;
    iTVPTexture2D* texture = provider->GetTexture();
    if (!texture) return TJS_E_FAIL;
    *output = texture->GetPitch();
    return TJS_S_OK;
}
