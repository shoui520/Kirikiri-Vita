#include "ncbind/ncbind.hpp"

#include "krkrvita/retail_bootstrap.hpp"

#include "mosaic.h"
#include "ripple.h"
#include "rotatetrans.h"
#include "turn.h"
#include "wave.h"

#define NCB_MODULE_NAME TJS_W("extrans.dll")

namespace {

void register_extrans() {
    RegisterWaveTransHandlerProvider();
    RegisterMosaicTransHandlerProvider();
    RegisterTurnTransHandlerProvider();
    RegisterRotateTransHandlerProvider();
    RegisterRippleTransHandlerProvider();
    krkrvita_boot_trace("retail-extrans-ready");
}

void unregister_extrans() {
    UnregisterRippleTransHandlerProvider();
    UnregisterRotateTransHandlerProvider();
    UnregisterTurnTransHandlerProvider();
    UnregisterMosaicTransHandlerProvider();
    UnregisterWaveTransHandlerProvider();
}

} // namespace

NCB_PRE_REGIST_CALLBACK(register_extrans);
NCB_POST_UNREGIST_CALLBACK(unregister_extrans);
