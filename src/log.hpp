#pragma once
#include <android/log.h>
// Minimal logger. Has info/debug/warn/error members so it can also be passed to
// beatsaber-hook's INSTALL_HOOK(logger, Name) whatever flavour of logger it expects.
struct SfqLogger {
    template <typename... A> void info (const char* f, A&&...) const { __android_log_print(ANDROID_LOG_INFO,  "SaberFactoryQuest", "%s", f); }
    template <typename... A> void debug(const char* f, A&&...) const { __android_log_print(ANDROID_LOG_DEBUG, "SaberFactoryQuest", "%s", f); }
    template <typename... A> void warn (const char* f, A&&...) const { __android_log_print(ANDROID_LOG_WARN,  "SaberFactoryQuest", "%s", f); }
    template <typename... A> void error(const char* f, A&&...) const { __android_log_print(ANDROID_LOG_ERROR, "SaberFactoryQuest", "%s", f); }
};
inline SfqLogger& getLogger() { static SfqLogger l; return l; }
#define SFQ_LOG(...)  __android_log_print(ANDROID_LOG_INFO,  "SaberFactoryQuest", __VA_ARGS__)
#define SFQ_ERR(...)  __android_log_print(ANDROID_LOG_ERROR, "SaberFactoryQuest", __VA_ARGS__)
