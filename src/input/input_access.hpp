#pragma once
#include "remote_input_queue.hpp"
#include <memory>

namespace render_module::detail {
// Private integration/test hook. Acquire ONLY on the render thread after Init.
// Producers retain this handle and touch only the thread-safe queue. Null for
// Desktop/uninitialized modules; retained handles report Closed after Shutdown.
std::shared_ptr<RemoteInputQueue> RemoteInputQueueHandle();
} // namespace render_module::detail
