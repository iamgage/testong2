#pragma once
#include <android/log.h>
#include <cstdarg>
#include <cstdio>
#include <sys/stat.h>

// Logs go to logcat AND to <mod folder>/log.txt, so they can be read without adb
// (headset storage: Internal shared storage / ModData / com.beatgames.beatsaber / Mods / SaberFactoryQuest / log.txt).
// log.txt is truncated each time the game starts.
inline const char* sfqLogPath() { return "/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberFactoryQuest/log.txt"; }

inline void sfqLogInit() {
    mkdir("/sdcard/ModData", 0777);
    mkdir("/sdcard/ModData/com.beatgames.beatsaber", 0777);
    mkdir("/sdcard/ModData/com.beatgames.beatsaber/Mods", 0777);
    mkdir("/sdcard/ModData/com.beatgames.beatsaber/Mods/SaberFactoryQuest", 0777);
    if (FILE* f = std::fopen(sfqLogPath(), "w")) { std::fputs("SaberFactory Quest log\n", f); std::fclose(f); }
}

__attribute__((format(printf, 2, 3)))
inline void sfqLogWrite(int prio, const char* fmt, ...) {
    char buf[1024];
    va_list ap; va_start(ap, fmt); std::vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    __android_log_print(prio, "SaberFactoryQuest", "%s", buf);
    if (FILE* f = std::fopen(sfqLogPath(), "a")) {
        std::fprintf(f, "%s %s\n", prio >= ANDROID_LOG_ERROR ? "ERROR" : "info ", buf);
        std::fclose(f);
    }
}

// beatsaber-hook's INSTALL_HOOK requires a "logger" with the usual level methods
// (info/debug/warn/error/critical/...), so every common level name is provided.
// Extra format arguments are ignored; only the message string is written.
struct SfqLogger {
#define SFQ_LEVEL(name, prio) \
    template <typename... A> void name(const char* f, A&&...) const { sfqLogWrite(prio, "%s", f); }
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
#define SFQ_LOG(...)  sfqLogWrite(ANDROID_LOG_INFO,  __VA_ARGS__)
#define SFQ_ERR(...)  sfqLogWrite(ANDROID_LOG_ERROR, __VA_ARGS__)
