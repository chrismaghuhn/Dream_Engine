#pragma once

namespace engine {

#if defined(_WIN32)
void crash_handler_install();
#else
inline void crash_handler_install() {}
#endif

} // namespace engine
