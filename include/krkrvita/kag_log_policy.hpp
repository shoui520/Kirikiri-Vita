#pragma once

namespace krkrvita {

// Retail scripts commonly request verbose KAG diagnostics unconditionally.
// Release builds suppress those debug logs completely; exceptions and normal
// fatal-error reporting remain independent of this setting.
constexpr int vita_kag_effective_debug_level(int) noexcept
{
	return 0;
}

constexpr bool vita_kag_should_emit_log(
	int requested, int required) noexcept
{
	return vita_kag_effective_debug_level(requested) >= required;
}

} // namespace krkrvita
