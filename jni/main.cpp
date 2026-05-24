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

struct ModInfo { const char* name,*version,*author,*package; uint8_t handlerVer; };
static ModInfo modinfo = {"AML FightNPC","12.0","Burhan","com.burhan.fightnpc",1};
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
typedef void  (*AddTaskPrimary_t)(void*,void*,bool);
typedef void* (*SimpleFightCtor_t)(void*,void*,int,unsigned int);

static FindPlayerPed_t   fnFindPlayerPed;
static GetPad_t          fnGetPad;
static SprintJustDown_t  fnSprint;
static AddPed_t          fnAddPed;
static WorldAdd_t        fnWorldAdd;
static AddTaskPrimary_t  fnAddTaskPrimary;
static SimpleFightCtor_t fnSimpleFightCtor;

static bool getPedPos(void* ped, CVector& out) {
    if(!ped) return false;
    void* mx=*(void**)((uint8_t*)ped+MATRIX_OFF);
    if(!mx) return false;
    out=*(CVector*)((uint8_t*)mx+POS_OFF);
    return (out.x!=0.0f||out.y!=0.0f);
}

static void assignFight(void* attacker, void* target) {
    if(!attacker||!target) return;
    void* intel=*(void**)((uint8_t*)attacker+INTEL_OFF);
    if(!intel) return;
    void* task=malloc(0x80);
    if(!task) return;
    memset(task,0,0x80);
    fnSimpleFightCtor(task, target, 0, 0);
    fnAddTaskPrimary(intel, task, false);
}

static void* pollThread(void*) {
    sleep(20); // tunggu game + pad fully init
    wlog("THREAD","started");

    uintptr_t base=getLibBase("libGTASA.so");
    if(!base){wlog("THREAD","no base");return nullptr;}
    char b[48]; snprintf(b,sizeof(b),"base:0x%08X",(unsigned)base); wlog("INIT",b);

    fnFindPlayerPed  =(FindPlayerPed_t)  FN(base,0x0040b288);
    fnGetPad         =(GetPad_t)         FN(base,0x003f8ca4);
    fnSprint         =(SprintJustDown_t) FN(base,0x003fbe14);
    fnAddPed         =(AddPed_t)         FN(base,0x004cf26c);
    fnWorldAdd       =(WorldAdd_t)       FN(base,0x004233c8);
    fnAddTaskPrimary =(AddTaskPrimary_t) FN(base,0x004c04c8);
    fnSimpleFightCtor=(SimpleFightCtor_t)FN(base,0x004d86b0);

    wlog("THREAD","polling");
    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}

        // Hanya null check — tidak ada logika pad ready lain
        void* pad=fnGetPad(0);
        if(!pad) continue;
        if(!fnSprint(pad)) continue;
        cd=120;
        wlog("SPAWN","trigger!");

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SPAWN","pos fail");continue;}

        char buf[64];
        snprintf(buf,sizeof(buf),"pos %.0f %.0f %.1f",pos.x,pos.y,pos.z);
        wlog("SPAWN",buf);

        CVector sp1={pos.x+3.0f, pos.y,      pos.z+0.5f};
        CVector sp2={pos.x+5.0f, pos.y+2.0f, pos.z+0.5f};

        void* npc1=fnAddPed(4, 0, &sp1, false);
        void* npc2=fnAddPed(4, 0, &sp2, false);

        snprintf(buf,sizeof(buf),"npc1=%p npc2=%p",npc1,npc2);
        wlog("SPAWN",buf);

        if(!npc1||!npc2){
            if(npc1) fnWorldAdd(npc1);
            if(npc2) fnWorldAdd(npc2);
            wlog("SPAWN","AddPed fail"); continue;
        }

        fnWorldAdd(npc1);
        fnWorldAdd(npc2);
        wlog("SPAWN","WorldAdd OK");

        usleep(200000);

        assignFight(npc1, npc2);
        assignFight(npc2, npc1);
        wlog("SPAWN","task assigned OK");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== FightNPC v12 ===");
    pthread_t t;
    pthread_create(&t,nullptr,pollThread,nullptr);
    pthread_detach(t);
}
