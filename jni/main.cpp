#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <android/log.h>
#include <sys/system_properties.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define TAG      "BURHAN_AML"
#define LOG_PATH "/sdcard/burhan_aml_log.txt"

// ── Structs ─────────────────────────────────────────────────────
struct ModInfo {
    const char* name; const char* version;
    const char* author; const char* package;
    uint8_t handlerVer;
};
static ModInfo modinfo = {"AML SpawnNPC","1.0","Burhan","com.burhan.spawnnpc",1};

struct CVector { float x, y, z; };
enum ePedType { PED_TYPE_CIVMALE = 4 };

// ── Offsets ──────────────────────────────────────────────────────
#define ADDR(x) ((void*)(x))
typedef void*  (*FindPlayerPed_t)(int);
typedef void*  (*GetPad_t)(int);
typedef int    (*CollectPickupJustDown_t)(void*);
typedef void*  (*AddPed_t)(int pedType, unsigned int model, const CVector*, bool);
typedef void   (*WorldAdd_t)(void*);
typedef void*  (*KillMelee_t)(void* task, void* targetPed);
typedef void   (*ClearTasks_t)(void* intel, bool, bool);
typedef void   (*AddTaskPrimary_t)(void* intel, void* task, bool);

static FindPlayerPed_t     fnFindPlayerPed    = (FindPlayerPed_t)    ADDR(0x0040b289); // +1 Thumb
static GetPad_t            fnGetPad           = (GetPad_t)           ADDR(0x003f8ca5);
static CollectPickupJustDown_t fnCollect       = (CollectPickupJustDown_t) ADDR(0x003fbf41);
static AddPed_t            fnAddPed           = (AddPed_t)           ADDR(0x004cf26d);
static WorldAdd_t          fnWorldAdd         = (WorldAdd_t)         ADDR(0x004233c9);
static ClearTasks_t        fnClearTasks       = (ClearTasks_t)       ADDR(0x004c08ed);
static AddTaskPrimary_t    fnAddTaskPrimary   = (AddTaskPrimary_t)   ADDR(0x004c04c9);

// KillPedOnFootMelee ctor — paling simple, hanya butuh target CPed*
// signature: void* ctor(void* taskMem, CPed* target)
static KillMelee_t fnKillMeleeCtor = (KillMelee_t) ADDR(0x004e17cd);

// VTable KillPedOnFootMelee (isi vtable pointer di task object)
static void** vtKillMelee = (void**) ADDR(0x006698c0);

#define PED_INTEL_OFFSET 0x47C  // CPed::m_pIntelligence

// ── Logger ───────────────────────────────────────────────────────
static void wlog(const char* lv, const char* msg) {
    __android_log_print(ANDROID_LOG_INFO, TAG, "[%s] %s", lv, msg);
    FILE* f = fopen(LOG_PATH, "a");
    if (!f) return;
    time_t now = time(NULL); struct tm* t = localtime(&now);
    char ts[16]; strftime(ts, sizeof(ts), "%H:%M:%S", t);
    fprintf(f, "[%s][%s] %s\n", ts, lv, msg);
    fclose(f);
}

// ── Dobby hook eglSwapBuffers ────────────────────────────────────
#include <dlfcn.h>
typedef void* (*DobbyHook_t)(void*, void*, void**);
static void* orig_eglSwapBuffers = nullptr;
static int   cooldown = 0;

static void* hook_eglSwapBuffers(void* dpy, void* surface) {
    typedef void* (*egl_t)(void*, void*);
    // poll tiap frame
    if (cooldown > 0) cooldown--;

    void* pad = fnGetPad(0);
    if (pad && cooldown == 0 && fnCollect(pad)) {
        cooldown = 180; // ~3 detik (60fps)
        wlog("SPAWN", "Trigger! Spawning NPC...");

        void* player = fnFindPlayerPed(0);
        if (!player) { wlog("SPAWN","FindPlayerPed null"); goto done; }

        // posisi player dari struct: CPlaceable matrix di CPed+0x14, pos di +0x30
        float* mat  = (float*)((uint8_t*)player + 0x14);
        CVector pos = { mat[12] + 2.0f, mat[13], mat[14] };

        // spawn ped model 0 (default)
        void* npc = fnAddPed(PED_TYPE_CIVMALE, 0, &pos, false);
        if (!npc) { wlog("SPAWN","AddPed null"); goto done; }
        fnWorldAdd(npc);

        // get intelligence
        void* intel = *(void**)((uint8_t*)npc + PED_INTEL_OFFSET);
        if (!intel) { wlog("SPAWN","intel null"); goto done; }
        fnClearTasks(intel, true, true);

        // alloc task (CTaskComplexKillPedOnFootMelee ~0x50 bytes)
        void* task = operator new(0x50);
        *(void**)task = vtKillMelee; // set vtable
        fnKillMeleeCtor(task, player);

        fnAddTaskPrimary(intel, task, false);
        wlog("SPAWN", "NPC spawned + kill task assigned!");
    }

done:
    return ((egl_t)orig_eglSwapBuffers)(dpy, surface);
}

// ── AML Exports ──────────────────────────────────────────────────
EXPORT ModInfo* __GetModInfo() { return &modinfo; }
EXPORT void OnModPreLoad() { wlog("PRELOAD","OnModPreLoad"); }

EXPORT void OnModLoad() {
    wlog("LOAD","=== SpawnNPC Mod LOADED ===");
    wlog("LOAD","Trigger: CollectPickupJustDown (tombol ambil pickup)");

    void* libEGL = dlopen("libEGL.so", RTLD_NOW);
    if (!libEGL) { wlog("LOAD","libEGL open fail"); return; }
    void* eglSwap = dlsym(libEGL, "eglSwapBuffers");
    if (!eglSwap) { wlog("LOAD","eglSwapBuffers sym fail"); return; }

    void* libDobby = dlopen("libdobby.so", RTLD_NOW);
    if (!libDobby) { wlog("LOAD","libdobby.so not found"); return; }
    auto DobbyHook = (DobbyHook_t)dlsym(libDobby, "DobbyHook");
    if (!DobbyHook) { wlog("LOAD","DobbyHook sym fail"); return; }

    DobbyHook(eglSwap, (void*)hook_eglSwapBuffers, &orig_eglSwapBuffers);
    wlog("LOAD","eglSwapBuffers hooked OK");
}
