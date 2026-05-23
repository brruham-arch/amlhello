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
static ModInfo modinfo = {"AML FightNPC","1.0","Burhan","com.burhan.fightnpc",1};
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

// Set ped flags: no flee, always fight
// m_nPedFlags di CPed biasanya sekitar +0x4C0
// Flag bits dari SA source:
//   bit 26 = bNotAllowedToAttack (0 = boleh attack)
//   bit 28 = bKindaStayInSamePlace
// Kita set via byte langsung di area flag ped
static void setFightFlags(void* ped) {
    if(!ped) return;
    // Scan offset flag yang kita tahu: +0x3C = 100.0 (health), +0x90 = 70 (ped type related)
    // Set surrender/flee immunity via known field:
    // nPedType di +0x22C (PC) — skip, terlalu riskan tanpa konfirmasi
    // Cukup andalkan task persist — jika task loop tidak selesai, ped tidak akan flee
    (void)ped;
}

static void assignKillTask(void* attacker, void* target) {
    void* intel = *(void**)((uint8_t*)attacker + INTEL_OFF);
    if(!intel) { wlog("TASK","intel null"); return; }

    fnClearTasks(intel, true, true);

    // Alokasi cukup besar, zero-init penuh
    // Jangan set vtable manual — biarkan ctor yang handle
    void* task = operator new(0x200);
    memset(task, 0, 0x200);

    // Ctor akan set vtable + inisialisasi semua field
    fnKillMeleeCtor(task, target);

    fnAddTaskPrimary(intel, task, false);
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
    fnClearTasks    =(ClearTasks_t)    FN(base,0x004c08ec);
    fnAddTaskPrimary=(AddTaskPrimary_t)FN(base,0x004c04c8);
    fnKillMeleeCtor =(KillMeleeCtor_t) FN(base,0x004e17cc);

    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}
        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=120; // 6 detik cooldown
        wlog("SPAWN","Trigger!");

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SPAWN","pos fail");continue;}

        char buf[80];
        snprintf(buf,sizeof(buf),"pos %.1f %.1f %.1f",pos.x,pos.y,pos.z);
        wlog("SPAWN",buf);

        // Spawn 2 NPC dengan jarak sedikit berpisah
        CVector sp1={pos.x+3.0f, pos.y,       pos.z};
        CVector sp2={pos.x+5.0f, pos.y+2.0f,  pos.z};

        void* npc1=fnAddPed(4,0,&sp1,false);
        void* npc2=fnAddPed(4,0,&sp2,false);

        if(!npc1||!npc2){wlog("SPAWN","AddPed fail");continue;}

        fnWorldAdd(npc1);
        fnWorldAdd(npc2);
        wlog("SPAWN","2 NPC added to world");

        // Cross-assign: NPC1 serang NPC2, NPC2 serang NPC1
        assignKillTask(npc1, npc2);
        wlog("SPAWN","npc1 -> target npc2");

        assignKillTask(npc2, npc1);
        wlog("SPAWN","npc2 -> target npc1");

        wlog("SPAWN","DONE — fight started!");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== FightNPC ===");
    pthread_t t; pthread_create(&t,nullptr,pollThread,nullptr); pthread_detach(t);
}
