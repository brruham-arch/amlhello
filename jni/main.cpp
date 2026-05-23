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
    FILE* f = fopen("/proc/self/maps","r");
    if (!f) return 0;
    char line[512]; uintptr_t base = 0;
    while (fgets(line,sizeof(line),f)) {
        if (strstr(line,name) && strstr(line,"r-xp")) {
            base = (uintptr_t)strtoul(line,nullptr,16); break;
        }
    }
    fclose(f); return base;
}

#define FN(base,off) (void*)((base)+(off)+1)
typedef void* (*FindPlayerPed_t)(int);
typedef void* (*GetPad_t)(int);
typedef int   (*SprintJustDown_t)(void*);
typedef void* (*AddPed_t)(int,unsigned int,const CVector*,bool);
typedef void  (*WorldAdd_t)(void*);
typedef void  (*KillMeleeCtor_t)(void*,void*);
typedef void  (*ClearTasks_t)(void*,bool,bool);
typedef void  (*AddTaskPrimary_t)(void*,void*,bool);

static FindPlayerPed_t   fnFindPlayerPed;
static GetPad_t          fnGetPad;
static SprintJustDown_t  fnSprint;
static AddPed_t          fnAddPed;
static WorldAdd_t        fnWorldAdd;
static ClearTasks_t      fnClearTasks;
static AddTaskPrimary_t  fnAddTaskPrimary;
static KillMeleeCtor_t   fnKillMeleeCtor;
static void**            vtKillMelee;

static bool initFunctions(uintptr_t base) {
    if (!base) return false;
    char buf[48]; snprintf(buf,sizeof(buf),"base: 0x%08X",(unsigned)base);
    wlog("INIT",buf);
    fnFindPlayerPed  = (FindPlayerPed_t) FN(base,0x0040b288);
    fnGetPad         = (GetPad_t)        FN(base,0x003f8ca4);
    fnSprint         = (SprintJustDown_t)FN(base,0x003fbe14);
    fnAddPed         = (AddPed_t)        FN(base,0x004cf26c);
    fnWorldAdd       = (WorldAdd_t)      FN(base,0x004233c8);
    fnClearTasks     = (ClearTasks_t)    FN(base,0x004c08ec);
    fnAddTaskPrimary = (AddTaskPrimary_t)FN(base,0x004c04c8);
    fnKillMeleeCtor  = (KillMeleeCtor_t) FN(base,0x004e17cc);
    vtKillMelee      = (void**)(base+0x006698c0);
    return true;
}

// Baca posisi dari CPed struct langsung — tidak perlu FindPlayerCoors
// CPlaceable: vtable@+0, m_matrix ptr@+4, matrix.pos@+0x30
static bool getPedPos(void* ped, CVector& out) {
    if (!ped) return false;
    void* matrix = *(void**)((uint8_t*)ped + 0x04);
    if (!matrix) return false;
    out = *(CVector*)((uint8_t*)matrix + 0x30);
    // sanity check — koordinat valid di peta GTA SA
    if (out.x < -3000 || out.x > 3000 ||
        out.y < -3000 || out.y > 3000) return false;
    return true;
}

static void* pollThread(void*) {
    sleep(10);
    wlog("THREAD","started");
    uintptr_t base = getLibBase("libGTASA.so");
    if (!initFunctions(base)) { wlog("THREAD","FATAL: no base"); return nullptr; }

    int cooldown = 0;
    while (true) {
        usleep(50000);
        if (cooldown > 0) { cooldown--; continue; }

        void* pad = fnGetPad(0);
        if (!pad || !fnSprint(pad)) continue;
        cooldown = 60;

        void* player = fnFindPlayerPed(0);
        CVector pos;
        if (!getPedPos(player, pos)) {
            wlog("SPAWN","pos invalid, skip"); continue;
        }

        char buf[64];
        snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f",pos.x,pos.y,pos.z);
        wlog("SPAWN",buf);

        CVector spawnPos = {pos.x+2.0f, pos.y, pos.z};
        void* npc = fnAddPed(4, 0, &spawnPos, false);
        if (!npc) { wlog("SPAWN","AddPed null"); continue; }
        fnWorldAdd(npc);
        wlog("SPAWN","WorldAdd OK");

        void* intel = *(void**)((uint8_t*)npc + 0x47C);
        if (!intel) { wlog("SPAWN","intel null"); continue; }
        fnClearTasks(intel,true,true);

        void* task = operator new(0x50);
        *(void**)task = vtKillMelee;
        fnKillMeleeCtor(task,player);
        fnAddTaskPrimary(intel,task,false);
        wlog("SPAWN","DONE!");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo() { return &modinfo; }
EXPORT void OnModPreLoad() { remove(LOG_PATH); wlog("PRELOAD","OK"); }
EXPORT void OnModLoad() {
    wlog("LOAD","=== SpawnNPC LOADED ===");
    pthread_t t;
    pthread_create(&t,nullptr,pollThread,nullptr);
    pthread_detach(t);
}
