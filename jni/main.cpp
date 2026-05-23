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

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static SprintJustDown_t fnSprint;
static AddPed_t         fnAddPed;
static WorldAdd_t       fnWorldAdd;

static bool getPedPos(void* ped, CVector& out) {
    if(!ped) return false;
    void* mx=*(void**)((uint8_t*)ped+0x14);
    if(!mx) return false;
    out=*(CVector*)((uint8_t*)mx+0x30);
    return (out.x!=0||out.y!=0);
}

static void* pollThread(void*) {
    sleep(10); wlog("THREAD","started");
    uintptr_t base=getLibBase("libGTASA.so");
    if(!base){wlog("THREAD","no base");return nullptr;}
    char b[48]; snprintf(b,sizeof(b),"base:0x%08X",(unsigned)base); wlog("INIT",b);

    fnFindPlayerPed=(FindPlayerPed_t)FN(base,0x0040b288);
    fnGetPad       =(GetPad_t)       FN(base,0x003f8ca4);
    fnSprint       =(SprintJustDown_t)FN(base,0x003fbe14);
    fnAddPed       =(AddPed_t)       FN(base,0x004cf26c);
    fnWorldAdd     =(WorldAdd_t)     FN(base,0x004233c8);

    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}
        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=120;

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SCAN","pos fail");continue;}

        CVector sp={pos.x+2.0f,pos.y,pos.z};
        void* npc=fnAddPed(4,0,&sp,false);
        if(!npc){wlog("SCAN","AddPed null");continue;}
        fnWorldAdd(npc);

        char buf[64];
        snprintf(buf,sizeof(buf),"npc=0x%08X",(unsigned)npc);
        wlog("SCAN",buf);

        // Scan offset 0x3C0~0x520: cari intelligence pointer
        // CPedIntelligence ptr biasanya di sekitar range ini
        for(int off=0x3C0;off<=0x520;off+=4){
            uint32_t raw=*(uint32_t*)((uint8_t*)npc+off);
            // Pointer valid di range Android heap
            if(raw>0xC0000000u&&raw<0xFF000000u){
                snprintf(buf,sizeof(buf),"  npc+0x%03X=0x%08X",off,raw);
                wlog("SCAN",buf);
            }
        }
        wlog("SCAN","---");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== INTEL SCAN ===");
    pthread_t t; pthread_create(&t,nullptr,pollThread,nullptr); pthread_detach(t);
}
