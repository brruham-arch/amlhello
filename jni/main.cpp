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
#define MATRIX_OFF 0x14
#define POS_OFF    0x30
#define INTEL_OFF  0x440
#define HEALTH_OFF 0x3C

// Slot task manager GTA SA:
// 0 = PRIMARY_DEFAULT (dipakai AI normal, GangFollower, dll)
// 4 = PRIMARY_SCRIPTED (dipakai script misi, aman dari AI override)
#define TASK_SLOT_SCRIPTED 4

struct ModInfo { const char* name,*version,*author,*package; uint8_t handlerVer; };
static ModInfo modinfo = {"AML FightNPC","7.0","Burhan","com.burhan.fightnpc",1};
struct CVector { float x,y,z; };

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
// CTaskManager::SetTask(CTask* task, int slot, bool immediate)
// intel == &m_TaskMgr (offset 0 di CPedIntelligence)
typedef void  (*SetTask_t)(void*,void*,int,bool);

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static SprintJustDown_t fnSprint;
static AddPed_t         fnAddPed;
static WorldAdd_t       fnWorldAdd;
static KillMeleeCtor_t  fnKillMeleeCtor;
static SetTask_t        fnSetTask;

static bool getPedPos(void* ped, CVector& out) {
    if(!ped) return false;
    void* mx=*(void**)((uint8_t*)ped+MATRIX_OFF);
    if(!mx) return false;
    out=*(CVector*)((uint8_t*)mx+POS_OFF);
    return true;
}

static void assignScriptedMelee(void* attacker, void* target) {
    if(!attacker||!target) return;
    void* intel=*(void**)((uint8_t*)attacker+INTEL_OFF);
    if(!intel) return;

    void* task=malloc(0x200);
    if(!task) return;
    memset(task,0,0x200);
    fnKillMeleeCtor(task, target);

    // Taruh di slot SCRIPTED (4) — aman dari GangFollower/AI override
    // intel == &m_TaskMgr karena m_TaskMgr ada di offset 0 CPedIntelligence
    fnSetTask(intel, task, TASK_SLOT_SCRIPTED, true);
}

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
    fnKillMeleeCtor =(KillMeleeCtor_t) FN(base,0x004e17cc);
    fnSetTask       =(SetTask_t)       FN(base,0x0053390a);

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

        CVector sp1={pos.x+3.0f, pos.y,      pos.z};
        CVector sp2={pos.x+5.0f, pos.y+2.0f, pos.z};

        void* npc1=fnAddPed(4,0,&sp1,false);
        void* npc2=fnAddPed(4,0,&sp2,false);

        if(!npc1||!npc2){
            if(npc1) fnWorldAdd(npc1);
            if(npc2) fnWorldAdd(npc2);
            wlog("SPAWN","AddPed fail"); continue;
        }

        fnWorldAdd(npc1);
        fnWorldAdd(npc2);

        // Assign ke slot SCRIPTED — tidak konflik dengan GangFollower di slot lain
        assignScriptedMelee(npc1, npc2);
        assignScriptedMelee(npc2, npc1);

        char buf[48];
        snprintf(buf,sizeof(buf),"spawned %.0f %.0f",pos.x,pos.y);
        wlog("SPAWN",buf);
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== FightNPC v7 — slot SCRIPTED ===");
    pthread_t t;
    pthread_create(&t,nullptr,pollThread,nullptr);
    pthread_detach(t);
}
