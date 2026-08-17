#pragma once

namespace krkrvita {

// Yuri normally treats any texture with more than one reference as shared and
// performs copy-on-write before modifying it.  A frontend presentation hold is
// different: it keeps the texture alive until the next presentation boundary,
// but it does not require snapshot semantics.  The Vita engine and presenter
// run serially on the main thread, so those read-only holds must not turn the
// persistent compositor buffer into a shared mutable bitmap.
constexpr bool yuri_texture_has_single_mutable_owner(
    int total_references, int presentation_references) noexcept {
    return total_references > 0 && presentation_references >= 0 &&
           total_references == presentation_references + 1;
}

} // namespace krkrvita
