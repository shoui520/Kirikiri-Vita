#pragma once

namespace TJS {
class tTJS;
}

namespace krkrvita {

// Installs the optional System.checkAppId compatibility property used by
// several retail executables. The property follows global.APP_ID unless an
// explicit patch assigns an override through System.checkAppId.
bool install_system_app_id_compat(TJS::tTJS& engine);

} // namespace krkrvita
