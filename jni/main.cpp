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

struct ModInfo { const char* name,*version,*author,*package; uint8_t handlerVer; };
static ModInfo modinfo = {"AML SpawnNPC","1.0","Burhan","com.burhan.spawnnpc",1};
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
// CTaskComplexKillPedOnFoot ctor(CPed* target, int, uint, uint, uint, int)
typedef void  (*KillPedCtor_t)(void*,void*,int,unsigned,unsigned,unsigned,int);
typedef void  (*ClearTasks_t)(void*,bool,bool);
typedef void  (*AddTaskPrimary_t)(void*,void*,bool);
// SetPedDecisionMaker sebagai fallback
typedef void  (*SetDecision_t)(void*,int);

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static SprintJustDown_t fnSprint;
static AddPed_t         fnAddPed;
static WorldAdd_t       fnWorldAdd;
static KillPedCtor_t    fnKillPedCtor;
static ClearTasks_t     fnClearTasks;
static AddTaskPrimary_t fnAddTaskPrimary;
static SetDecision_t    fnSetDecision;
static void**           vtKillPed;  // CTaskComplexKillPedOnFoot vtable

#define MATRIX_OFF  0x14
#define POS_OFF     0x30
#define INTEL_OFF   0x440

static bool getPedPos(void* ped, CVector& out) {
    if(!ped) return false;
    void* mx=*(void**)((uint8_t*)ped+MATRIX_OFF);
    if(!mx) return false;
    out=*(CVector*)((uint8_t*)mx+POS_OFF);
    return (out.x!=0||out.y!=0);
}

static void* pollThread(void*) {
    sleep(10); wlog("THREAD","started");
    uintptr_t base=getLibBase("libGTASA.so");
    if(!base){wlog("THREAD","no base");return nullptr;}
    char b[48]; snprintf(b,sizeof(b),"base:0x%08X",(unsigned)base); wlog("INIT",b);

    fnFindPlayerPed =(FindPlayerPed_t) FN(base,0x0040b288);
    fnGetPad        =(GetPad_t)        FN(base,0x003f8ca4);
    fnSprint        =(SprintJustDown_t)FN(base,0x003fbe14);
    fnAddPed        =(AddPed_t)        FN(base,0x004cf26c);
    fnWorldAdd      =(WorldAdd_t)      FN(base,0x004233c8);
    // CTaskComplexKillPedOnFoot (bukan Melee)
    fnKillPedCtor   =(KillPedCtor_t)   FN(base,0x004e01b0);
    fnClearTasks    =(ClearTasks_t)    FN(base,0x004c08ec);
    fnAddTaskPrimary=(AddTaskPrimary_t)FN(base,0x004c04c8);
    fnSetDecision   =(SetDecision_t)   FN(base,0x004be294);
    vtKillPed       =(void**)(base+0x00669848); // CTaskComplexKillPedOnFoot vtable

    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}
        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=120; // 6 detik cooldown — cukup waktu animasi siap

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SPAWN","pos fail");continue;}

        char buf[64];
        snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f",pos.x,pos.y,pos.z);
        wlog("SPAWN",buf);

        CVector sp={pos.x+2.0f,pos.y,pos.z};
        void* npc=fnAddPed(4,0,&sp,false);
        if(!npc){wlog("SPAWN","AddPed null");continue;}
        fnWorldAdd(npc);

        // Kasih waktu game init model NPC sebelum assign task
        usleep(500000); // 500ms

        void* intel=*(void**)((uint8_t*)npc+INTEL_OFF);
        if(!intel){wlog("SPAWN","intel null");continue;}

        fnClearTasks(intel,true,true);

        // Buat task: CTaskComplexKillPedOnFoot(target, 0, 0, 0, 0, 0)
        void* task=operator new(0x100);
        memset(task,0,0x100);
        *(void**)task=vtKillPed;
        fnKillPedCtor(task,player,0,0,0,0,0);
        fnAddTaskPrimary(intel,task,false);

        wlog("SPAWN","NPC hostile spawned!");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== SpawnNPC v3 ===");
    pthread_t t; pthread_create(&t,nullptr,pollThread,nullptr); pthread_detach(t);
}
