#include "krkrvita/bubble.hpp"

#include <psp2/promoterutil.h>
#include <psp2/sysmodule.h>

#include <cstdint>

namespace krkrvita {
namespace {

int load_paf() {
    std::uint32_t arguments[] = {0x180000, 0xffffffff, 0xffffffff, 1,
                                 0xffffffff, 0xffffffff};
    int module_result = -1;
    SceSysmoduleOpt option{};
    option.flags = sizeof(option);
    option.result = &module_result;
    option.unused[0] = -1;
    option.unused[1] = -1;
    return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,
                                                  sizeof(arguments), arguments, &option);
}

void unload_paf() {
    SceSysmoduleOpt option{};
    sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF,
                                             0, nullptr, &option);
}

} // namespace

int install_staged_bubble(const std::filesystem::path& staging_root) {
    int result = load_paf();
    if (result < 0) return result;
    result = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (result >= 0) result = scePromoterUtilityInit();
    if (result >= 0) result = scePromoterUtilityPromotePkgWithRif(staging_root.string().c_str(), 1);
    scePromoterUtilityExit();
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    unload_paf();
    return result;
}

} // namespace krkrvita
