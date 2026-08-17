#pragma once

#include <utility>

namespace krkrvita {

// Keep the overwhelmingly common one-task render path as the original
// callable.  In particular, do not first erase it into std::function: a large
// capture may allocate before a conventional dispatcher can inspect the task
// count.  The parallel adapter is invoked only for an explicit multi-task
// request and owns whatever type erasure that backend requires.
template <typename Task, typename ParallelDispatch>
inline void dispatch_render_tasks(int task_count, Task&& task,
                                  ParallelDispatch&& parallel_dispatch) {
    if (task_count <= 1) {
        if (task_count == 1) std::forward<Task>(task)(0);
        return;
    }
    std::forward<ParallelDispatch>(parallel_dispatch)(
        task_count, std::forward<Task>(task));
}

}  // namespace krkrvita
