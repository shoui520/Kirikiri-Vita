#pragma once

#include <string_view>

namespace krkrvita {

// Vita has no optical-drive namespace, but the selected retail project is a
// complete mounted game volume. A non-empty searchCD label therefore maps to
// that project path instead of failing every disc-protected startup script.
// Empty labels retain the desktop API's "not found" result.
inline bool virtual_cd_is_present(std::string_view volume_label,
                                  std::string_view project_path) noexcept {
    return !volume_label.empty() && !project_path.empty();
}

} // namespace krkrvita
