#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
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

static void wlog(const char* lv, const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "[%s] %s", lv, msg);
    FILE* f = fopen(LOG_PATH, "a");
    if (!f) return;
    time_t now = time(NULL); struct tm* t = localtime(&now);
    char ts[16]; strftime(ts,sizeof(ts),"%H:%M:%S",t);
    fprintf(f,"[%s][%s] %s\n",ts,lv,msg); fclose(f);
}

// ── Cari base libGTASA.so dari /proc/self/maps ───────────────────
static uintptr_t getLibBase(const char* name) {
    FILE* f = fopen("/proc/self/maps", "r");
    if (!f) return 0;
    char line[512]; uintptr_t base = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, name) && strstr(line, "r-xp")) {
            base = (uintptr_t)strtoul(line, nullptr, 16);
            break;
        }
    }
    fclose(f); return base;
}

// ── Function pointers (offset + base) ───────────────────────────
#define OFF(base, off) ((base) + (off) + 1) // +1 Thumb
typedef void* (*FindPlayerPed_t)(int);
typedef void* (*GetPad_t)(int);
typedef int   (*CollectJustDown_t)(void*);
typedef void* (*AddPed_t)(int, unsigned int, const CVector*, bool);
typedef void  (*WorldAdd_t)(void*);
typedef void  (*KillMeleeCtor_t)(void*, void*);
typedef void  (*ClearTasks_t)(void*, bool, bool);
typedef void  (*AddTaskPrimary_t)(void*, void*, bool);

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static CollectJustDown_t fnCollect;
static AddPed_t         fnAddPed;
static WorldAdd_t       fnWorldAdd;
static ClearTasks_t     fnClearTasks;
static AddTaskPrimary_t fnAddTaskPrimary;
static KillMeleeCtor_t  fnKillMeleeCtor;
static void**           vtKillMelee;

static bool initFunctions(uintptr_t base) {
    char buf[64];
    snprintf(buf,sizeof(buf),"libGTASA base: 0x%08X",(unsigned)base);
    wlog("INIT", buf);
    if (!base) return false;

    fnFindPlayerPed  = (FindPlayerPed_t) OFF(base, 0x0040b288);
    fnGetPad         = (GetPad_t)        OFF(base, 0x003f8ca4);
    fnCollect        = (CollectJustDown_t)OFF(base, 0x003fbf40);
    fnAddPed         = (AddPed_t)        OFF(base, 0x004cf26c);
    fnWorldAdd       = (WorldAdd_t)      OFF(base, 0x004233c8);
    fnClearTasks     = (ClearTasks_t)    OFF(base, 0x004c08ec);
    fnAddTaskPrimary = (AddTaskPrimary_t)OFF(base, 0x004c04c8);
    fnKillMeleeCtor  = (KillMeleeCtor_t) OFF(base, 0x004e17cc);
    vtKillMelee      = (void**)(base + 0x006698c0);
    return true;
}

// ── Poll thread ──────────────────────────────────────────────────
static void* pollThread(void*) {
    // tunggu game fully loaded
    sleep(5);
    wlog("THREAD","Poll thread started");

    uintptr_t base = getLibBase("libGTASA.so");
    if (!initFunctions(base)) {
        wlog("THREAD","FATAL: libGTASA base not found");
        return nullptr;
    }

    int cooldown = 0;
    while (true) {
        usleep(50000);
        if (cooldown > 0) { cooldown--; continue; }

        void* pad = fnGetPad(0);
        if (!pad || !fnCollect(pad)) continue;

        cooldown = 60;
        wlog("SPAWN","Trigger!");

        void* player = fnFindPlayerPed(0);
        if (!player) { wlog("SPAWN","player null"); continue; }

        float* mat = (float*)((uint8_t*)player + 0x14);
        CVector pos = { mat[12]+2.0f, mat[13], mat[14] };

        char buf[64];
        snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f",pos.x,pos.y,pos.z);
        wlog("SPAWN",buf);

        void* npc = fnAddPed(4, 0, &pos, false);
        if (!npc) { wlog("SPAWN","AddPed null"); continue; }
        fnWorldAdd(npc);

        void* intel = *(void**)((uint8_t*)npc + 0x47C);
        if (!intel) { wlog("SPAWN","intel null"); continue; }
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
    pthread_create(&t, nullptr, pollThread, nullptr);
    pthread_detach(t);
}
