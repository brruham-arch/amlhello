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
typedef void  (*ClearTasks_t)(void*,bool,bool);
// SetPedDecisionMakerType(int type)
typedef void  (*SetDecision_t)(void*,int);
// SetSeeingRange + SetHearingRange biar agresif penuh
typedef void  (*SetRange_t)(void*,float);

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static SprintJustDown_t fnSprint;
static AddPed_t         fnAddPed;
static WorldAdd_t       fnWorldAdd;
static ClearTasks_t     fnClearTasks;
static SetDecision_t    fnSetDecision;
static SetRange_t       fnSetSeeing;
static SetRange_t       fnSetHearing;

#define MATRIX_OFF 0x14
#define POS_OFF    0x30
#define INTEL_OFF  0x440

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

    fnFindPlayerPed=(FindPlayerPed_t) FN(base,0x0040b288);
    fnGetPad       =(GetPad_t)        FN(base,0x003f8ca4);
    fnSprint       =(SprintJustDown_t)FN(base,0x003fbe14);
    fnAddPed       =(AddPed_t)        FN(base,0x004cf26c);
    fnWorldAdd     =(WorldAdd_t)      FN(base,0x004233c8);
    fnClearTasks   =(ClearTasks_t)    FN(base,0x004c08ec);
    fnSetDecision  =(SetDecision_t)   FN(base,0x004be294);
    fnSetSeeing    =(SetRange_t)      FN(base,0x004c02d2);
    fnSetHearing   =(SetRange_t)      FN(base,0x004c02cc);

    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}
        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=120;
        wlog("SPAWN","Trigger!");

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SPAWN","pos fail");continue;}

        char buf[64];
        snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f",pos.x,pos.y,pos.z);
        wlog("SPAWN",buf);

        // Spawn 2 NPC random di sekitar player — biarkan mereka saling serang
        for(int i=0;i<2;i++){
            float ox = (i==0) ? 3.0f : -3.0f;
            CVector sp={pos.x+ox, pos.y+2.0f, pos.z};
            void* npc=fnAddPed(4,0,&sp,false);
            if(!npc) continue;
            fnWorldAdd(npc);

            void* intel=*(void**)((uint8_t*)npc+INTEL_OFF);
            if(!intel) continue;

            // Kosongkan task default
            fnClearTasks(intel,true,true);

            // Decision maker type 3 = sangat agresif (GANG_MEMBER hostile)
            // Ini biarkan game handle AI sendiri — tidak ada manual task
            fnSetDecision(intel,3);

            // Extend seeing & hearing range biar cepat detect musuh
            fnSetSeeing(intel, 50.0f);
            fnSetHearing(intel, 50.0f);
        }
        wlog("SPAWN","2 NPC spawned, AI handled by game");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== SpawnNPC v4 (no manual task) ===");
    pthread_t t; pthread_create(&t,nullptr,pollThread,nullptr); pthread_detach(t);
}
