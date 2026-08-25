#include "krkrvita/kag_log_policy.hpp"

#include <cassert>

int main()
{
	using krkrvita::vita_kag_effective_debug_level;
	using krkrvita::vita_kag_should_emit_log;

	static_assert(vita_kag_effective_debug_level(0) == 0);
	static_assert(vita_kag_effective_debug_level(1) == 0);
	static_assert(vita_kag_effective_debug_level(2) == 0);
	static_assert(vita_kag_effective_debug_level(99) == 0);

	// Neither simple nor verbose KAG debug diagnostics are emitted.
	static_assert(!vita_kag_should_emit_log(2, 1));
	static_assert(!vita_kag_should_emit_log(2, 2));
	static_assert(!vita_kag_should_emit_log(1, 2));
	static_assert(!vita_kag_should_emit_log(0, 1));

	assert(!vita_kag_should_emit_log(1, 1));
	assert(!vita_kag_should_emit_log(0, 1));
	return 0;
}
