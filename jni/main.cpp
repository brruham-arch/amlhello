#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define TAG      "BURHAN_AML"
#define LOG_PATH "/sdcard/burhan_aml_log.txt"

struct ModInfo {
    const char* name; const char* version;
    const char* author; const char* package;
    uint8_t handlerVer;
};
static ModInfo modinfo = {"AML SpawnNPC","1.0","Burhan","com.burhan.spawnnpc",1};

struct CVector { float x, y, z; };

// ── Function pointers ────────────────────────────────────────────
#define FN(addr, type) ((type)(addr | 1)) // +1 = Thumb
typedef void* (*FindPlayerPed_t)(int);
typedef void* (*GetPad_t)(int);
typedef int   (*CollectJustDown_t)(void*);
typedef void* (*AddPed_t)(int, unsigned int, const CVector*, bool);
typedef void  (*WorldAdd_t)(void*);
typedef void  (*KillMeleeCtor_t)(void*, void*);
typedef void  (*ClearTasks_t)(void*, bool, bool);
typedef void  (*AddTaskPrimary_t)(void*, void*, bool);

static auto fnFindPlayerPed  = FN(0x0040b288, FindPlayerPed_t);
static auto fnGetPad         = FN(0x003f8ca4, GetPad_t);
static auto fnCollect        = FN(0x003fbf40, CollectJustDown_t);
static auto fnAddPed         = FN(0x004cf26c, AddPed_t);
static auto fnWorldAdd       = FN(0x004233c8, WorldAdd_t);
static auto fnClearTasks     = FN(0x004c08ec, ClearTasks_t);
static auto fnAddTaskPrimary = FN(0x004c04c8, AddTaskPrimary_t);
static auto fnKillMeleeCtor  = FN(0x004e17cc, KillMeleeCtor_t);
static void** vtKillMelee    = (void**)0x006698c0;
#define INTEL_OFF 0x47C

static void wlog(const char* lv, const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "[%s] %s", lv, msg);
    FILE* f = fopen(LOG_PATH, "a");
    if (!f) return;
    time_t now = time(NULL); struct tm* t = localtime(&now);
    char ts[16]; strftime(ts,sizeof(ts),"%H:%M:%S",t);
    fprintf(f,"[%s][%s] %s\n",ts,lv,msg); fclose(f);
}

// ── Poll thread — 50ms, mirip wait(50) di Lua ────────────────────
static void* pollThread(void*) {
    wlog("THREAD","Poll thread started");
    int cooldown = 0;
    while (true) {
        usleep(50000); // 50ms
        if (cooldown > 0) { cooldown--; continue; }

        void* pad = fnGetPad(0);
        if (!pad || !fnCollect(pad)) continue;

        cooldown = 60; // 3 detik (60 * 50ms)
        wlog("SPAWN","Trigger fired!");

        void* player = fnFindPlayerPed(0);
        if (!player) { wlog("SPAWN","player null"); continue; }

        // posisi player: matrix di +0x14, translation di +0x30 (col 3)
        float* mat = (float*)((uint8_t*)player + 0x14);
        CVector pos = { mat[12] + 2.0f, mat[13], mat[14] };
        char buf[64]; snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f", pos.x,pos.y,pos.z);
        wlog("SPAWN", buf);

        void* npc = fnAddPed(4, 0, &pos, false);
        if (!npc) { wlog("SPAWN","AddPed null"); continue; }
        fnWorldAdd(npc);

        void* intel = *(void**)((uint8_t*)npc + INTEL_OFF);
        if (!intel) { wlog("SPAWN","intel null — cek INTEL_OFF"); continue; }
        fnClearTasks(intel, true, true);

        void* task = operator new(0x50);
        *(void**)task = vtKillMelee;
        fnKillMeleeCtor(task, player);
        fnAddTaskPrimary(intel, task, false);

        wlog("SPAWN","NPC spawned!");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo() { return &modinfo; }
EXPORT void OnModPreLoad() { wlog("PRELOAD","OK"); }
EXPORT void OnModLoad() {
    wlog("LOAD","=== SpawnNPC Mod LOADED ===");
    pthread_t t;
    if (pthread_create(&t, nullptr, pollThread, nullptr) == 0) {
        pthread_detach(t);
        wlog("LOAD","Poll thread created");
    } else {
        wlog("LOAD","pthread_create failed");
    }
}
