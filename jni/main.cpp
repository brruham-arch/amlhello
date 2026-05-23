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

static FindPlayerPed_t  fnFindPlayerPed;
static GetPad_t         fnGetPad;
static SprintJustDown_t fnSprint;

static void* pollThread(void*) {
    sleep(10); wlog("THREAD","started");
    uintptr_t base=getLibBase("libGTASA.so");
    if(!base){wlog("THREAD","no base");return nullptr;}
    char b[48]; snprintf(b,sizeof(b),"base:0x%08X",(unsigned)base); wlog("INIT",b);
    fnFindPlayerPed=(FindPlayerPed_t)FN(base,0x0040b288);
    fnGetPad=(GetPad_t)FN(base,0x003f8ca4);
    fnSprint=(SprintJustDown_t)FN(base,0x003fbe14);

    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}
        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=60;

        void* ped=fnFindPlayerPed(0);
        if(!ped){wlog("SCAN","ped null");continue;}
        snprintf(b,sizeof(b),"ped=0x%08X",(unsigned)ped); wlog("SCAN",b);

        // Scan ped+0x00 ~ +0xA0
        for(int off=0;off<=0xA0;off+=4){
            uint32_t raw=*(uint32_t*)((uint8_t*)ped+off);
            float fv=*(float*)&raw;
            if(raw>0x80000000u&&raw<0xFF000000u){
                snprintf(b,sizeof(b),"  +0x%02X ptr=0x%08X",off,raw); wlog("SCAN",b);
            } else if((fv>10||fv<-10)&&fv>-5000&&fv<5000){
                snprintf(b,sizeof(b),"  +0x%02X flt=%.1f",off,fv); wlog("SCAN",b);
            }
        }

        // Scan isi matrix di ptr+0x14
        void* mx=*(void**)((uint8_t*)ped+0x14);
        if(mx){
            snprintf(b,sizeof(b),"  mx=0x%08X",(unsigned)mx); wlog("SCAN",b);
            for(int mo=0;mo<=0x50;mo+=4){
                float fv=*(float*)((uint8_t*)mx+mo);
                if((fv>10||fv<-10)&&fv>-5000&&fv<5000){
                    snprintf(b,sizeof(b),"  mx+0x%02X=%.1f",mo,fv); wlog("SCAN",b);
                }
            }
        }
        wlog("SCAN","---");
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== SCAN ===");
    pthread_t t; pthread_create(&t,nullptr,pollThread,nullptr); pthread_detach(t);
}
