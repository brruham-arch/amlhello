#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define TAG      "BURHAN_AML"
#define LOG_PATH "/sdcard/burhan_aml_log.txt"
#define MAX_NPCS    32
#define MATRIX_OFF  0x14
#define POS_OFF     0x30
#define INTEL_OFF   0x440
#define HEALTH_OFF  0x3C   // dari scan: +0x3C=100.0

struct ModInfo { const char* name,*version,*author,*package; uint8_t handlerVer; };
static ModInfo modinfo = {"AML FightNPC","2.0","Burhan","com.burhan.fightnpc",1};
struct CVector { float x,y,z; };

// ── NPC registry ─────────────────────────────────────────────────
static void*         g_npcs[MAX_NPCS] = {};
static int           g_npcCount = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static void wlog(const char* lv, const char* msg) {
    __android_log_print(ANDROID_LOG_INFO,TAG,"[%s] %s",lv,msg);
    FILE* f=fopen(LOG_PATH,"a"); if(!f) return;
    time_t now=time(NULL); struct tm* t=localtime(&now);
    char ts[16]; strftime(ts,sizeof(ts),"%H:%M:%S",t);
    fprintf(f,"[%s][%s] %s\n",ts,lv,msg); fclose(f);
}
static uintptr_t getLibBase(const char* name) {
    FILE* f=fopen("/proc/self/maps","r"); if(!f) return 0;
    char line[512]; uintptr_t base=0;
    while(fgets(line,sizeof(line),f))
        if(strstr(line,name)&&strstr(line,"r-xp")){base=(uintptr_t)strtoul(line,nullptr,16);break;}
    fclose(f); return base;
}

#define FN(b,o) (void*)((b)+(o)+1)
typedef void* (*FindPlayerPed_t)(int);
typedef void* (*GetPad_t)(int);
typedef int   (*SprintJustDown_t)(void*);
typedef void* (*AddPed_t)(int,unsigned int,const CVector*,bool);
typedef void  (*WorldAdd_t)(void*);
typedef void  (*KillMeleeCtor_t)(void*,void*);
typedef void  (*ClearTasks_t)(void*,bool,bool);
typedef void  (*AddTaskPrimary_t)(void*,void*,bool);

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static SprintJustDown_t fnSprint;
static AddPed_t         fnAddPed;
static WorldAdd_t       fnWorldAdd;
static ClearTasks_t     fnClearTasks;
static AddTaskPrimary_t fnAddTaskPrimary;
static KillMeleeCtor_t  fnKillMeleeCtor;

// ── Helper ────────────────────────────────────────────────────────
static bool getPedPos(void* ped, CVector& out) {
    if(!ped) return false;
    void* mx=*(void**)((uint8_t*)ped+MATRIX_OFF);
    if(!mx) return false;
    out=*(CVector*)((uint8_t*)mx+POS_OFF);
    return true;
}

// Ped dianggap hidup jika health > 0
static bool isPedAlive(void* ped) {
    if(!ped) return false;
    float hp = *(float*)((uint8_t*)ped + HEALTH_OFF);
    return hp > 0.1f;
}

static void assignKillTask(void* attacker, void* target) {
    if(!attacker || !target) return;
    if(!isPedAlive(attacker) || !isPedAlive(target)) return;

    void* intel = *(void**)((uint8_t*)attacker + INTEL_OFF);
    if(!intel) return;

    fnClearTasks(intel, true, true);

    void* task = operator new(0x200);
    memset(task, 0, 0x200);
    fnKillMeleeCtor(task, target);
    fnAddTaskPrimary(intel, task, false);
}

// ── Monitor thread — reassign task tiap 500ms ─────────────────────
// Efek: anti-flee + target baru otomatis saat target mati
static void* monitorThread(void*) {
    sleep(12);
    wlog("MONITOR","started");

    while(true) {
        usleep(500000); // 500ms

        // Snapshot registry dengan lock
        pthread_mutex_lock(&g_lock);
        int count = g_npcCount;
        void* snap[MAX_NPCS];
        memcpy(snap, g_npcs, sizeof(void*)*count);
        pthread_mutex_unlock(&g_lock);

        if(count < 2) continue;

        // Cari semua NPC yang masih hidup
        void* alive[MAX_NPCS];
        int   aliveCount = 0;
        for(int i=0;i<count;i++)
            if(isPedAlive(snap[i]))
                alive[aliveCount++] = snap[i];

        if(aliveCount < 2) continue;

        // Tiap NPC hidup → serang NPC hidup lain secara round-robin
        for(int i=0;i<aliveCount;i++) {
            // target = NPC berikutnya dalam list (round-robin)
            int targetIdx = (i+1) % aliveCount;
            assignKillTask(alive[i], alive[targetIdx]);
        }

        char buf[32];
        snprintf(buf,sizeof(buf),"alive: %d",aliveCount);
        wlog("MON",buf);
    }
    return nullptr;
}

// ── Spawn thread — poll trigger ───────────────────────────────────
static void* pollThread(void*) {
    sleep(10);
    wlog("THREAD","started");
    uintptr_t base=getLibBase("libGTASA.so");
    if(!base){wlog("THREAD","no base");return nullptr;}
    char b[48]; snprintf(b,sizeof(b),"base:0x%08X",(unsigned)base); wlog("INIT",b);

    fnFindPlayerPed =(FindPlayerPed_t) FN(base,0x0040b288);
    fnGetPad        =(GetPad_t)        FN(base,0x003f8ca4);
    fnSprint        =(SprintJustDown_t)FN(base,0x003fbe14);
    fnAddPed        =(AddPed_t)        FN(base,0x004cf26c);
    fnWorldAdd      =(WorldAdd_t)      FN(base,0x004233c8);
    fnClearTasks    =(ClearTasks_t)    FN(base,0x004c08ec);
    fnAddTaskPrimary=(AddTaskPrimary_t)FN(base,0x004c04c8);
    fnKillMeleeCtor =(KillMeleeCtor_t) FN(base,0x004e17cc);

    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}
        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=120;

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SPAWN","pos fail");continue;}

        pthread_mutex_lock(&g_lock);
        if(g_npcCount >= MAX_NPCS-1){
            wlog("SPAWN","registry full, reset");
            g_npcCount = 0;
            memset(g_npcs, 0, sizeof(g_npcs));
        }

        // Spawn 2 NPC baru setiap trigger
        CVector sp1={pos.x+3.0f, pos.y,      pos.z};
        CVector sp2={pos.x+5.0f, pos.y+2.0f, pos.z};

        void* npc1=fnAddPed(4,0,&sp1,false);
        void* npc2=fnAddPed(4,0,&sp2,false);

        char buf[64];
        if(npc1){ fnWorldAdd(npc1); g_npcs[g_npcCount++]=npc1; }
        if(npc2){ fnWorldAdd(npc2); g_npcs[g_npcCount++]=npc2; }

        snprintf(buf,sizeof(buf),"spawned, total registry: %d",g_npcCount);
        wlog("SPAWN",buf);
        pthread_mutex_unlock(&g_lock);

        // Assign task awal langsung (monitor akan maintain)
        if(npc1&&npc2){
            assignKillTask(npc1,npc2);
            assignKillTask(npc2,npc1);
        }
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== FightNPC v2 ===");
    pthread_t t1,t2;
    pthread_create(&t1,nullptr,pollThread,nullptr);   pthread_detach(t1);
    pthread_create(&t2,nullptr,monitorThread,nullptr); pthread_detach(t2);
}
