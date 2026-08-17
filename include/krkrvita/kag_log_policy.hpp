#pragma once

namespace krkrvita {

// KAG defines 0=none, 1=simple scenario diagnostics and 2=verbose per-tag
// diagnostics.  Retail games commonly request level 2 unconditionally.  A
// desktop can absorb that volume, but synchronous logging of every tag turns
// large macro libraries into minutes of apparent startup hangs on Vita.
//
// Preserve level 1 so scenario loads, jumps and returns remain diagnosable;
// only clamp the per-line/macro/call-stack flood.  This policy does not touch
// Debug.notice, Debug.logAsError or exception reporting.
constexpr int vita_kag_effective_debug_level(int requested) noexcept
{
	return requested > 1 ? 1 : requested;
}

constexpr bool vita_kag_should_emit_log(
	int requested, int required) noexcept
{
	return vita_kag_effective_debug_level(requested) >= required;
}

} // namespace krkrvita
