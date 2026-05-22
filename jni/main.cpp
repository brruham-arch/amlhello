#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/system_properties.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define TAG     "BURHAN_AML"
#define LOG_PATH "/sdcard/burhan_aml_log.txt"

struct ModInfo {
    const char* name;
    const char* version;
    const char* author;
    const char* package;
    uint8_t     handlerVer;
};

static ModInfo modinfo = {
    "AML Hello", "1.0", "Burhan", "com.burhan.amlhello", 1
};

static void wlog(const char* level, const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "[%s] %s", level, msg);
    FILE* f = fopen(LOG_PATH, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", t);
    fprintf(f, "[%s][%s] %s\n", ts, level, msg);
    fclose(f);
}

EXPORT ModInfo* __GetModInfo() { return &modinfo; }

EXPORT void OnModPreLoad() {
    wlog("PRELOAD", "OnModPreLoad called");
}

EXPORT void OnModLoad() {
    wlog("LOAD", "=== AML Hello LOADED ===");
    char buf[64];
    char prop[PROP_VALUE_MAX];
    __system_property_get("ro.build.version.release", prop);
    snprintf(buf, sizeof(buf), "Android: %s", prop);
    wlog("LOAD", buf);
    __system_property_get("ro.product.model", prop);
    snprintf(buf, sizeof(buf), "Device: %s", prop);
    wlog("LOAD", buf);
    snprintf(buf, sizeof(buf), "PID: %d", (int)getpid());
    wlog("LOAD", buf);
    wlog("LOAD", "Log: " LOG_PATH);
}
