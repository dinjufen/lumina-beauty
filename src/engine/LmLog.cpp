#include "LmLog.hpp"

#include <atomic>
#include <cstdio>

namespace {

std::atomic<lumina_log_callback> g_callback{nullptr};
std::atomic<void*> g_user{nullptr};

} // namespace

LmLogLine::~LmLogLine() {
    const std::string text = "[lumina] " + m_stream.str();
    lumina_log_callback callback = g_callback.load();
    if (callback) {
        callback(m_level, text.c_str(), g_user.load());
    } else if (m_level != LUMINA_LOG_INFO) {
        fprintf(stderr, "%s\n", text.c_str());
    }
}

extern "C" LUMINA_API void lumina_set_log_callback(lumina_log_callback callback, void* user) {
    g_user = user;
    g_callback = callback;
}
