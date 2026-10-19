#pragma once
#include <android/log.h>

// Minimal logger. beatsaber-hook's INSTALL_HOOK requires a "logger" with the usual level methods
// (info/debug/warn/error/critical/...), so every common level name is provided.
// Extra format arguments are ignored; only the message string is written.
struct SfqLogger {
#define SFQ_LEVEL(name, prio) \
    template <typename... A> void name(const char* f, A&&...) const { __android_log_print(prio, "SaberFactoryQuest", "%s", f); }
    SFQ_LEVEL(trace,    ANDROID_LOG_VERBOSE)
    SFQ_LEVEL(debug,    ANDROID_LOG_DEBUG)
    SFQ_LEVEL(info,     ANDROID_LOG_INFO)
    SFQ_LEVEL(warn,     ANDROID_LOG_WARN)
    SFQ_LEVEL(warning,  ANDROID_LOG_WARN)
    SFQ_LEVEL(error,    ANDROID_LOG_ERROR)
    SFQ_LEVEL(critical, ANDROID_LOG_FATAL)
    SFQ_LEVEL(fatal,    ANDROID_LOG_FATAL)
#undef SFQ_LEVEL
};
inline SfqLogger& getLogger() { static SfqLogger l; return l; }
#define SFQ_LOG(...)  __android_log_print(ANDROID_LOG_INFO,  "SaberFactoryQuest", __VA_ARGS__)
#define SFQ_ERR(...)  __android_log_print(ANDROID_LOG_ERROR, "SaberFactoryQuest", __VA_ARGS__)
