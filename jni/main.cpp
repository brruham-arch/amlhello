#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define FN(base, off) (void*)((base) + (off) + 1)

typedef void*    (*FindPlayerPed_t)(int);
// FindPlayerCoords(int) → CVector* (ptr ke static buffer)
typedef CVector* (*FindPlayerCoords_t)(int);
typedef void*    (*GetPad_t)(int);
typedef int      (*SprintJustDown_t)(void*);
typedef void*    (*AddPed_t)(int, unsigned int, const CVector*, bool);
typedef void     (*WorldAdd_t)(void*);
typedef void     (*KillMeleeCtor_t)(void*, void*);
typedef void     (*ClearTasks_t)(void*, bool, bool);
typedef void     (*AddTaskPrimary_t)(void*, void*, bool);

static FindPlayerPed_t    fnFindPlayerPed;
static FindPlayerCoords_t fnFindPlayerCoords;
static GetPad_t           fnGetPad;
static SprintJustDown_t   fnSprint;
static AddPed_t           fnAddPed;
static WorldAdd_t         fnWorldAdd;
static ClearTasks_t       fnClearTasks;
static AddTaskPrimary_t   fnAddTaskPrimary;
static KillMeleeCtor_t    fnKillMeleeCtor;
static void**             vtKillMelee;

static bool initFunctions(uintptr_t base) {
    char buf[64];
    snprintf(buf,sizeof(buf),"base: 0x%08X",(unsigned)base);
    wlog("INIT",buf);
    if (!base) return false;
    fnFindPlayerPed    = (FindPlayerPed_t)    FN(base, 0x0040b288);
    fnFindPlayerCoords = (FindPlayerCoords_t) FN(base, 0x0040b5dc);
    fnGetPad           = (GetPad_t)           FN(base, 0x003f8ca4);
    fnSprint           = (SprintJustDown_t)   FN(base, 0x003fbe14);
    fnAddPed           = (AddPed_t)           FN(base, 0x004cf26c);
    fnWorldAdd         = (WorldAdd_t)         FN(base, 0x004233c8);
    fnClearTasks       = (ClearTasks_t)       FN(base, 0x004c08ec);
    fnAddTaskPrimary   = (AddTaskPrimary_t)   FN(base, 0x004c04c8);
    fnKillMeleeCtor    = (KillMeleeCtor_t)    FN(base, 0x004e17cc);
    vtKillMelee        = (void**)(base + 0x006698c0);
    return true;
}

static void* pollThread(void*) {
    sleep(10);
    wlog("THREAD","started");

    uintptr_t base = getLibBase("libGTASA.so");
    if (!initFunctions(base)) {
        wlog("THREAD","FATAL: base not found"); return nullptr;
    }

    int cooldown = 0;
    while (true) {
        usleep(50000);
        if (cooldown > 0) { cooldown--; continue; }

        void* pad = fnGetPad(0);
        if (!pad || !fnSprint(pad)) continue;
        cooldown = 60;

        // posisi via FindPlayerCoords — aman, no matrix math
        void* _chk = fnFindPlayerPed(0);
        if (!_chk) { wlog("SPAWN","player null, skip"); continue; }
        CVector* ppos = fnFindPlayerCoords(0);
        if (!ppos || (ppos->x == 0 && ppos->y == 0)) {
            wlog("SPAWN","coords invalid"); continue;
        }
        CVector spawnPos = { ppos->x + 2.0f, ppos->y, ppos->z };

        char buf[80];
        snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f",spawnPos.x,spawnPos.y,spawnPos.z);
        wlog("SPAWN",buf);

        void* npc = fnAddPed(4, 0, &spawnPos, false);
        if (!npc) { wlog("SPAWN","AddPed null"); continue; }
        fnWorldAdd(npc);
        wlog("SPAWN","WorldAdd OK");

        void* intel = *(void**)((uint8_t*)npc + 0x47C);
        if (!intel) { wlog("SPAWN","intel null"); continue; }
        fnClearTasks(intel, true, true);
        wlog("SPAWN","ClearTasks OK");

        void* player = fnFindPlayerPed(0);
        void* task = operator new(0x50);
        *(void**)task = vtKillMelee;
        fnKillMeleeCtor(task, player);
        fnAddTaskPrimary(intel, task, false);
        wlog("SPAWN","DONE — NPC spawned!");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo() { return &modinfo; }
EXPORT void OnModPreLoad() {
    remove(LOG_PATH); // hapus log lama
    wlog("PRELOAD","OK");
}
EXPORT void OnModLoad() {
    wlog("LOAD","=== SpawnNPC Mod LOADED ===");
    pthread_t t;
    pthread_create(&t, nullptr, pollThread, nullptr);
    pthread_detach(t);
}
