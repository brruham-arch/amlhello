#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/system_properties.h>

// ─── AML ModInfo Interface ──────────────────────────────────────────────────
// handlerVer = 1 wajib cocok dengan AML engine
struct ModInfo {
    const char* name;
    const char* version;
    const char* author;
    const char* package;
    uint8_t     handlerVer;
};

static ModInfo modinfo = {
    "AML Hello",          // name
    "1.0",                // version
    "Burhan",             // author
    "com.burhan.amlhello",// package
    1                     // handlerVer — WAJIB = 1
};

// ─── Logger ────────────────────────────────────────────────────────────────
#define TAG     "BURHAN_AML"
#define LOG_PATH "/sdcard/burhan_aml_log.txt"

static void wlog(const char* level, const char* msg) {
    // Logcat
    __android_log_print(ANDROID_LOG_INFO, TAG, "[%s] %s", level, msg);

    // File log ke /sdcard
    FILE* f = fopen(LOG_PATH, "a");
    if (!f) return;

    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", t);

    fprintf(f, "[%s][%s] %s\n", ts, level, msg);
    fclose(f);
}

static void log_sysinfo() {
    char buf[128];
    char prop[PROP_VALUE_MAX];

    // Android version
    __system_property_get("ro.build.version.release", prop);
    snprintf(buf, sizeof(buf), "Android: %s", prop);
    wlog("INFO", buf);

    // Device model
    __system_property_get("ro.product.model", prop);
    snprintf(buf, sizeof(buf), "Device: %s", prop);
    wlog("INFO", buf);

    // ABI
    __system_property_get("ro.product.cpu.abi", prop);
    snprintf(buf, sizeof(buf), "ABI: %s", prop);
    wlog("INFO", buf);

    // PID
    snprintf(buf, sizeof(buf), "PID: %d", (int)getpid());
    wlog("INFO", buf);
}

// ─── AML Exports ───────────────────────────────────────────────────────────
extern "C" ModInfo* __GetModInfo() {
    return &modinfo;
}

extern "C" void OnModPreLoad() {
    wlog("PRELOAD", "OnModPreLoad called");
    wlog("PRELOAD", "AML engine recognized the mod");
}

extern "C" void OnModLoad() {
    wlog("LOAD", "========================================");
    wlog("LOAD", "  AML Hello Mod - LOADED SUCCESSFULLY!");
    wlog("LOAD", "  Name    : AML Hello v1.0");
    wlog("LOAD", "  Author  : Burhan");
    wlog("LOAD", "  Handler : 1");
    wlog("LOAD", "========================================");
    log_sysinfo();
    wlog("LOAD", "Log file: " LOG_PATH);
    wlog("LOAD", "OnModLoad selesai.");
}
