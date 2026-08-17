#pragma once

#include <cstddef>
#include <memory>
#include <utility>

namespace krkrvita {

template <typename TArgument>
TArgument* allocate_event_argument_storage(std::size_t count)
{
    return count == 0 ? nullptr : new TArgument[count];
}

template <typename TArgument, typename TInvoke>
void with_event_argument_pointers(TArgument* arguments,
                                  std::size_t count,
                                  TInvoke&& invoke)
{
    if(count == 0) {
        std::forward<TInvoke>(invoke)(nullptr);
        return;
    }

    std::unique_ptr<TArgument*[]> argument_pointers(
        new TArgument*[count]);
    for(std::size_t index = 0; index < count; ++index)
        argument_pointers[index] = arguments + index;
    std::forward<TInvoke>(invoke)(argument_pointers.get());
}

} // namespace krkrvita
