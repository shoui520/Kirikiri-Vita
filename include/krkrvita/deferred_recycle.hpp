#pragma once

#include <cstddef>

namespace krkrvita {

// A recycler may defer objects released by a destructor into a new batch so
// it can iterate the current batch without invalidation.  At an allocation-
// failure barrier, keep taking separate batches until one recycles nothing.
// The monotonic completion counter avoids exposing the queue implementation.
template <typename CompletionCounter, typename RecycleBatch>
std::size_t drain_deferred_recycle_batches(CompletionCounter completed,
                                           RecycleBatch recycle) {
    std::size_t nonempty_batches = 0;
    for (;;) {
        const auto before = completed();
        recycle();
        if (completed() == before) return nonempty_batches;
        ++nonempty_batches;
    }
}

} // namespace krkrvita
