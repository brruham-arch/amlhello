#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>
#include <math.h>

#define EXPORT extern "C" __attribute__((visibility("default")))
#define TAG      "BURHAN_AML"
#define LOG_PATH "/sdcard/burhan_aml_log.txt"
#define MATRIX_OFF 0x14
#define POS_OFF    0x30
#define INTEL_OFF  0x440
#define HEALTH_OFF 0x3C
#define MAX_NPCS   32
#define CHASE_DIST 6.0f   // jarak mulai kejar
#define FIGHT_DIST 3.0f   // jarak mulai serang

struct ModInfo { const char* name,*version,*author,*package; uint8_t handlerVer; };
static ModInfo modinfo = {"AML FightNPC","13.0","Burhan","com.burhan.fightnpc",1};
struct CVector { float x,y,z; };

// Registry NPC — simpan pasangan
static void* g_npcs[MAX_NPCS] = {};
static int   g_count = 0;
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
typedef void  (*AddTaskPrimary_t)(void*,void*,bool);
// CTaskSimpleFight(CEntity* target, int type, uint flags)
typedef void* (*SimpleFightCtor_t)(void*,void*,int,unsigned int);
// CTaskSimpleGoToPoint(int moveState, CVector& pos, float speed, bool b1, bool b2)
// moveState: 1=walk, 4=run, 5=sprint
typedef void* (*GoToPointCtor_t)(void*,int,const CVector*,float,bool,bool);

static FindPlayerPed_t   fnFindPlayerPed;
static GetPad_t          fnGetPad;
static SprintJustDown_t  fnSprint;
static AddPed_t          fnAddPed;
static WorldAdd_t        fnWorldAdd;
static AddTaskPrimary_t  fnAddTaskPrimary;
static SimpleFightCtor_t fnSimpleFightCtor;
static GoToPointCtor_t   fnGoToPointCtor;

static bool getPedPos(void* ped, CVector& out) {
    if(!ped) return false;
    void* mx=*(void**)((uint8_t*)ped+MATRIX_OFF);
    if(!mx) return false;
    out=*(CVector*)((uint8_t*)mx+POS_OFF);
    return (out.x!=0.0f||out.y!=0.0f);
}

static bool isPedAlive(void* ped) {
    if(!ped) return false;
    float hp=*(float*)((uint8_t*)ped+HEALTH_OFF);
    return hp > 0.1f;
}

static float dist2d(const CVector& a, const CVector& b) {
    float dx=a.x-b.x, dy=a.y-b.y;
    return sqrtf(dx*dx+dy*dy);
}

// Assign task kejar target — NPC berlari ke posisi target
static void assignGoTo(void* npc, const CVector& dest) {
    void* intel=*(void**)((uint8_t*)npc+INTEL_OFF);
    if(!intel) return;
    void* task=malloc(0x80);
    if(!task) return;
    memset(task,0,0x80);
    // moveState=4 (run), speed=1.0f
    fnGoToPointCtor(task, 4, &dest, 1.0f, false, false);
    fnAddTaskPrimary(intel, task, false);
}

// Assign task serang target
static void assignFight(void* npc, void* target) {
    void* intel=*(void**)((uint8_t*)npc+INTEL_OFF);
    if(!intel) return;
    void* task=malloc(0x80);
    if(!task) return;
    memset(task,0,0x80);
    fnSimpleFightCtor(task, target, 1, 0);
    fnAddTaskPrimary(intel, task, false);
}

// State machine per NPC: kejar atau serang
static void updateNpc(void* npc, void* target) {
    if(!isPedAlive(npc)||!isPedAlive(target)) return;

    CVector posA, posB;
    if(!getPedPos(npc,posA)||!getPedPos(target,posB)) return;

    float d = dist2d(posA, posB);

    if(d > CHASE_DIST) {
        // Masih jauh — kejar dulu
        assignGoTo(npc, posB);
    } else {
        // Sudah dekat — serang
        assignFight(npc, target);
    }
}

// State machine thread — jalan tiap 800ms
static void* stateThread(void*) {
    sleep(22);
    wlog("STATE","started");
    while(true) {
        usleep(800000); // 800ms

        pthread_mutex_lock(&g_lock);
        int count = g_count;
        void* snap[MAX_NPCS];
        memcpy(snap, g_npcs, sizeof(void*)*count);
        pthread_mutex_unlock(&g_lock);

        if(count < 2) continue;

        // Kumpulkan NPC hidup
        void* alive[MAX_NPCS]; int ac=0;
        for(int i=0;i<count;i++)
            if(isPedAlive(snap[i])) alive[ac++]=snap[i];

        if(ac < 2) continue;

        // Update tiap NPC: target = NPC berikutnya (round-robin)
        for(int i=0;i<ac;i++)
            updateNpc(alive[i], alive[(i+1)%ac]);
    }
    return nullptr;
}

// Spawn + trigger thread
static void* pollThread(void*) {
    sleep(20);
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
    fnGoToPointCtor  =(GoToPointCtor_t)  FN(base,0x0051ca8c);

    wlog("THREAD","polling");
    int cd=0;
    while(true){
        usleep(50000);
        if(cd>0){cd--;continue;}

        void* pad=fnGetPad(0);
        if(!pad||!fnSprint(pad)) continue;
        cd=120;
        wlog("SPAWN","trigger!");

        void* player=fnFindPlayerPed(0);
        CVector pos;
        if(!getPedPos(player,pos)){wlog("SPAWN","pos fail");continue;}

        pthread_mutex_lock(&g_lock);
        if(g_count>=MAX_NPCS-1){ g_count=0; memset(g_npcs,0,sizeof(g_npcs)); }

        CVector sp1={pos.x+3.0f, pos.y,      pos.z+0.5f};
        CVector sp2={pos.x+5.0f, pos.y+2.0f, pos.z+0.5f};

        void* npc1=fnAddPed(4,0,&sp1,false);
        void* npc2=fnAddPed(4,0,&sp2,false);

        if(!npc1||!npc2){
            if(npc1) fnWorldAdd(npc1);
            if(npc2) fnWorldAdd(npc2);
            wlog("SPAWN","fail");
            pthread_mutex_unlock(&g_lock);
            continue;
        }

        fnWorldAdd(npc1);
        fnWorldAdd(npc2);
        g_npcs[g_count++]=npc1;
        g_npcs[g_count++]=npc2;
        pthread_mutex_unlock(&g_lock);

        usleep(200000);

        // Assign task awal langsung
        updateNpc(npc1, npc2);
        updateNpc(npc2, npc1);

        char buf[48];
        snprintf(buf,sizeof(buf),"spawned @ %.0f %.0f",pos.x,pos.y);
        wlog("SPAWN",buf);
    }
    return nullptr;
}

EXPORT ModInfo* __GetModInfo(){return &modinfo;}
EXPORT void OnModPreLoad(){remove(LOG_PATH);wlog("PRELOAD","OK");}
EXPORT void OnModLoad(){
    wlog("LOAD","=== FightNPC v13 — state machine ===");
    pthread_t t1,t2;
    pthread_create(&t1,nullptr,pollThread,nullptr);   pthread_detach(t1);
    pthread_create(&t2,nullptr,stateThread,nullptr);  pthread_detach(t2);
}
