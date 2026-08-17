#include "krkrvita/kag_log_policy.hpp"

#include <cassert>

int main()
{
	using krkrvita::vita_kag_effective_debug_level;
	using krkrvita::vita_kag_should_emit_log;

	static_assert(vita_kag_effective_debug_level(0) == 0);
	static_assert(vita_kag_effective_debug_level(1) == 1);
	static_assert(vita_kag_effective_debug_level(2) == 1);
	static_assert(vita_kag_effective_debug_level(99) == 1);

	// A retail request for tkdlVerbose still emits tkdlSimple scenario
	// diagnostics, but never emits tkdlVerbose per-tag diagnostics.
	static_assert(vita_kag_should_emit_log(2, 1));
	static_assert(!vita_kag_should_emit_log(2, 2));
	static_assert(!vita_kag_should_emit_log(1, 2));
	static_assert(!vita_kag_should_emit_log(0, 1));

	assert(vita_kag_should_emit_log(1, 1));
	assert(!vita_kag_should_emit_log(0, 1));
	return 0;
}
