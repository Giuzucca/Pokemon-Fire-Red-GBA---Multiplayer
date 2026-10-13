#include <mgba/core/core.h>
#include <mgba/core/scripting.h>
#include <mgba/script/context.h>
#include <mgba-util/vfs.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Instance { struct mCore *c; struct mScriptContext ctx; color_t pixels[240*160]; };
static unsigned symbol(const char *file,const char *name) {
    FILE *f=fopen(file,"r"); assert(f);
    char line[512], tail[128]; unsigned address=0;
    while (fgets(line,sizeof(line),f)) {
        char *p=strrchr(line,' ');
        if (p && sscanf(p,"%127s",tail)==1 && !strcmp(tail,name)) {
            sscanf(line,"%x",&address); break;
        }
    }
    fclose(f); assert(address); return address;
}
static void init(struct Instance *i,const char *rom,const char *script) {
    i->c=mCoreFind(rom); assert(i->c && i->c->init(i->c));
    mCoreInitConfig(i->c,NULL);
    i->c->setVideoBuffer(i->c,i->pixels,240);
    i->c->setAudioBufferSize(i->c,1024);
    assert(mCoreLoadFile(i->c,rom));
    i->c->reset(i->c);
    mScriptContextInit(&i->ctx);
    mScriptContextRegisterEngines(&i->ctx);
    mScriptContextAttachStdlib(&i->ctx);
    mScriptContextAttachSocket(&i->ctx);
    mScriptContextAttachCore(&i->ctx,i->c);
    mScriptContextAttachLogger(&i->ctx,NULL);
    assert(mScriptContextLoadFile(&i->ctx,script));
    struct mScriptEngineContext *engine=HashTableLookup(&i->ctx.engines,"lua");
    assert(engine);
    if (!engine->run(engine)) { fprintf(stderr,"Lua: %s\n",engine->getError(engine)); abort(); }
}
static void frames(struct Instance *a,struct Instance *b,unsigned n) {
    while (n--) {
        a->c->runFrame(a->c); mScriptContextTriggerCallback(&a->ctx,"frame");
        b->c->runFrame(b->c);
        mScriptContextTriggerCallback(&b->ctx,"frame");
    }
}
static void screenshot(struct Instance *a,const char *path) {
    struct VFile *vf=VFileOpen(path,O_WRONLY|O_CREAT|O_TRUNC); assert(vf);
    assert(mCoreTakeScreenshotVF(a->c,vf)); vf->close(vf);
}
static void report(struct Instance *i,unsigned m) {
    struct mCore *c=i->c;
    printf("magic=%08x tick=%u connected=%u object=%u ready=%u map=%u/%u xy=%u,%u rx=%u\n",
        c->busRead32(c,m),c->busRead32(c,m+8),c->busRead8(c,m+12),c->busRead8(c,m+13),
        c->busRead8(c,m+16),c->busRead8(c,m+17),c->busRead8(c,m+18),
        c->busRead16(c,m+20),c->busRead16(c,m+22),c->busRead32(c,m+40));
    fflush(stdout);
    printf("invite=%u/%u remote=%u/%u result=%u\n",c->busRead8(c,m+52),c->busRead32(c,m+44),
        c->busRead8(c,m+53),c->busRead32(c,m+48),c->busRead8(c,m+55)); fflush(stdout);
    printf("duel=%u remote=%u tx=%u ack=%u mask=%u abort=%u outcome=%u\n",c->busRead8(c,m+74),
        c->busRead8(c,m+75),c->busRead32(c,m+64),c->busRead32(c,m+68),c->busRead8(c,m+76),
        c->busRead8(c,m+78),c->busRead8(c,m+77)); fflush(stdout);
}
static void walkTo(struct Instance *a,struct Instance *b,unsigned m,unsigned x,unsigned y) {
    for (int n=0;n<300;n++) {
        unsigned cx=a->c->busRead16(a->c,m+20),cy=a->c->busRead16(a->c,m+22);
        if (cx==x && cy==y) { a->c->setKeys(a->c,0); frames(a,b,24); return; }
        unsigned key=cx<x ? 4 : cx>x ? 5 : cy<y ? 7 : 6;
        a->c->setKeys(a->c,1<<key); frames(a,b,1);
    }
    report(a,m); assert(!"test route obstructed");
}
static void runCode(struct Instance *i,const char *code) {
    struct VFile *vf=VFileFromConstMemory(code,strlen(code));
    assert(mScriptContextLoadVF(&i->ctx,"test.lua",vf)); vf->close(vf);
    struct mScriptEngineContext *engine=HashTableLookup(&i->ctx.engines,"lua");
    if (!engine->run(engine)) { fprintf(stderr,"Lua: %s\n",engine->getError(engine)); abort(); }
}
static void tap(struct Instance *who,struct Instance *a,struct Instance *b,unsigned key,unsigned settle) {
    who->c->setKeys(who->c,1<<key); frames(a,b,1);
    who->c->setKeys(who->c,0); frames(a,b,settle);
}
static void beginInvitation(struct Instance *a,struct Instance *b,unsigned m) {
    tap(a,a,b,0,160); // A: interact with adjacent visitor
    if(a->c->busRead8(a->c,m+52)!=1) {
        report(a,m); report(b,m);
        screenshot(a,"multiplayer/tests/invite-failed.png");
    }
    assert(a->c->busRead8(a->c,m+52)==1);
    tap(a,a,b,0,160); // YES: send
    report(a,m); report(b,m);
    screenshot(a,"multiplayer/tests/invitation-sender.png");
    screenshot(b,"multiplayer/tests/invitation-received.png");
    assert(a->c->busRead8(a->c,m+52)==2);
    assert(b->c->busRead8(b->c,m+52)==3);
    assert(a->c->busRead32(a->c,m+44)==b->c->busRead32(b->c,m+44));
}
static void closeInvitation(struct Instance *a,struct Instance *b) {
    tap(a,a,b,0,60); tap(b,a,b,0,60);
}
static void seedParty(struct Instance *who,struct Instance *a,struct Instance *b,const char *syms,unsigned species) {
    struct mCore *c=who->c;
    unsigned main=symbol(syms,"gMain"),party=symbol(syms,"gPlayerParty"),enemy=symbol(syms,"gEnemyParty");
    unsigned cb1=c->busRead32(c,main),cb2=c->busRead32(c,main+4);
    c->busWrite16(c,symbol(syms,"gSpecialVar_0x8004"),species);
    c->busWrite16(c,symbol(syms,"gSpecialVar_0x8005"),5);
    c->busWrite16(c,symbol(syms,"gSpecialVar_0x8006"),0);
    c->busWrite32(c,main,0); c->busWrite32(c,main+4,symbol(syms,"CreateEnemyEventMon")|1);
    frames(a,b,3);
    // A video-frame boundary may interrupt CreateMon. Stop scheduling it,
    // then let the in-flight call finish before copying its output.
    c->busWrite32(c,main,cb1); c->busWrite32(c,main+4,cb2); frames(a,b,3);
    for(unsigned j=0;j<600;j++) c->busWrite8(c,party+j,c->busRead8(c,enemy+j));
    c->busWrite8(c,symbol(syms,"gPlayerPartyCount"),1);
    frames(a,b,30);
    printf("seed species=%u count=%u hp=%u\n",species,c->busRead8(c,symbol(syms,"gPlayerPartyCount")),c->busRead16(c,party+86)); fflush(stdout);
    assert(c->busRead16(c,party+86)>0);
}
static int atAction(struct Instance *i,const char *syms,unsigned player) {
    static unsigned funcs,action;
    if(!funcs) { funcs=symbol(syms,"gBattlerControllerFuncs"); action=symbol(syms,"HandleInputChooseAction")|1; }
    return i->c->busRead32(i->c,funcs+4*player)==action;
}
static unsigned read32le(const unsigned char *p) {
    return p[0]|p[1]<<8|p[2]<<16|(unsigned)p[3]<<24;
}
static unsigned monExperience(const unsigned char *mon) {
    static const unsigned char growth[]={0,0,0,0,0,0,1,1,2,3,2,3,1,1,2,3,2,3,1,1,2,3,2,3};
    unsigned personality=read32le(mon),key=personality^read32le(mon+4);
    unsigned checksum=0;
    for(unsigned i=0;i<12;i++) {
        unsigned v=read32le(mon+32+i*4)^key;
        checksum+=(v&65535)+(v>>16);
    }
    assert((checksum&65535)==(unsigned)(mon[28]|mon[29]<<8));
    return read32le(mon+32+12*growth[personality%24]+4)^key;
}
static void seedBench(struct Instance *who,struct Instance *a,struct Instance *b,const char *syms,unsigned species) {
    unsigned party=symbol(syms,"gPlayerParty");
    unsigned char original[600],bench[100];
    for(unsigned j=0;j<600;j++) original[j]=who->c->busRead8(who->c,party+j);
    seedParty(who,a,b,syms,species);
    for(unsigned j=0;j<100;j++) bench[j]=who->c->busRead8(who->c,party+j);
    for(unsigned j=0;j<600;j++) who->c->busWrite8(who->c,party+j,original[j]);
    for(unsigned j=0;j<100;j++) who->c->busWrite8(who->c,party+100+j,bench[j]);
    who->c->busWrite8(who->c,symbol(syms,"gPlayerPartyCount"),2);
}
static void awaitBattle(struct Instance *a,struct Instance *b,unsigned m,const char *syms) {
    // No gameplay decisions are injected: both controllers must show FIGHT.
    unsigned funcs=symbol(syms,"gBattlerControllerFuncs"),action=symbol(syms,"HandleInputChooseAction")|1;
    for(unsigned n=0;n<4000;n++) {
        if(a->c->busRead32(a->c,funcs)==action && b->c->busRead32(b->c,funcs+4)==action) {
            frames(a,b,30); // Let both text windows finish their initial draw.
            puts("PASS: native link battle reaches independent action menus on both ROMs"); return;
        }
        frames(a,b,1);
    }
    report(a,m); report(b,m);
    printf("battle callbacks %08x %08x flags %08x %08x\n",a->c->busRead32(a->c,symbol(syms,"gMain")+4),b->c->busRead32(b->c,symbol(syms,"gMain")+4),a->c->busRead32(a->c,symbol(syms,"gBattleControllerExecFlags")),b->c->busRead32(b->c,symbol(syms,"gBattleControllerExecFlags")));
    screenshot(a,"multiplayer/tests/duel-host.png"); screenshot(b,"multiplayer/tests/duel-client.png");
    assert(!"native battle did not reach action menus");
}
static void playDuel(struct Instance *a,struct Instance *b,unsigned m,const char *syms) {
    unsigned party=symbol(syms,"gPlayerParty");
    unsigned char beforeA[600],beforeB[600];
    unsigned ptr1=symbol(syms,"gSaveBlock1Ptr"),ptr2=symbol(syms,"gSaveBlock2Ptr");
    unsigned char story[2][0x320]; unsigned money[2];
    struct Instance *players[2]={a,b};
    for(unsigned p=0;p<2;p++) {
        struct mCore *c=players[p]->c;
        unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
        money[p]=c->busRead32(c,save+0x290)^key;
        for(unsigned j=0;j<0x320;j++) story[p][j]=c->busRead8(c,save+0xee0+j);
    }
    for(unsigned i=0;i<600;i++) { beforeA[i]=a->c->busRead8(a->c,party+i); beforeB[i]=b->c->busRead8(b->c,party+i); }
    beginInvitation(a,b,m); tap(b,a,b,0,180); awaitBattle(a,b,m,syms);
    screenshot(a,"multiplayer/tests/duel-host.png"); screenshot(b,"multiplayer/tests/duel-client.png");
    unsigned hpB=b->c->busRead16(b->c,party+86);
    tap(a,a,b,0,120); tap(a,a,b,0,120);
    assert(atAction(b,syms,1));
    assert(b->c->busRead16(b->c,party+86)==hpB);
    puts("PASS: host input cannot choose the guest action or advance the turn alone");
    int damaged=0;
    for(unsigned n=0;n<15000;n++) {
        if(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4) break;
        a->c->setKeys(a->c,n%30==0 ? 1:0); b->c->setKeys(b->c,n%30==0 ? 1:0);
        frames(a,b,1);
        if(b->c->busRead16(b->c,party+86)<hpB) damaged=1;
    }
    a->c->setKeys(a->c,0); b->c->setKeys(b->c,0); frames(a,b,180);
    report(a,m); report(b,m);
    screenshot(a,"multiplayer/tests/duel-return.png");
    assert(damaged);
    assert(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4);
    assert(!a->c->busRead8(a->c,m+78) && !b->c->busRead8(b->c,m+78));
    unsigned resultA=a->c->busRead8(a->c,m+77),resultB=b->c->busRead8(b->c,m+77);
    assert((resultA==1 && resultB==2)||(resultA==2 && resultB==1));
    for(unsigned i=0;i<600;i++) { assert(beforeA[i]==a->c->busRead8(a->c,party+i)); assert(beforeB[i]==b->c->busRead8(b->c,party+i)); }
    for(unsigned p=0;p<2;p++) {
        struct mCore *c=players[p]->c;
        unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
        assert((c->busRead32(c,save+0x290)^key)==money[p]);
        for(unsigned j=0;j<0x320;j++) assert(story[p][j]==c->busRead8(c,save+0xee0+j));
    }
    closeInvitation(a,b);
    puts("PASS: real moves, damage, victory/defeat, return to map and byte-exact party restoration");
    puts("PASS: duel leaves both campaigns' flags, variables and money unchanged");
}
static void duelRecoveryTests(struct Instance *a,struct Instance *b,unsigned m,const char *syms,const char *clientScript) {
    unsigned party=symbol(syms,"gPlayerParty");
    unsigned char before[2][600];
    for(unsigned i=0;i<600;i++) { before[0][i]=a->c->busRead8(a->c,party+i); before[1][i]=b->c->busRead8(b->c,party+i); }
    unsigned previous=a->c->busRead32(a->c,m+56);
    beginInvitation(a,b,m); tap(b,a,b,0,180); awaitBattle(a,b,m,syms);
    assert(a->c->busRead32(a->c,m+56)!=previous);
    unsigned enemy=symbol(syms,"gEnemyParty");
    for(unsigned i=0;i<600;i++) {
        // Battle entry can adjust local friendship, so compare public slot identity
        // using personality, which must come from the opposite player's team.
        if(i%100<4) {
            assert(a->c->busRead8(a->c,enemy+i)==before[1][i]);
            assert(b->c->busRead8(b->c,enemy+i)==before[0][i]);
        }
    }
    tap(a,a,b,7,10); tap(a,a,b,0,180); // POKEMON
    screenshot(a,"multiplayer/tests/duel-party.png");
    tap(a,a,b,7,30); tap(a,a,b,0,60); tap(a,a,b,0,180); // second slot, SHIFT
    tap(b,a,b,0,90); tap(b,a,b,0,90);
    unsigned indexes=symbol(syms,"gBattlerPartyIndexes");
    for(unsigned n=0;n<3000;n++) {
        if(a->c->busRead16(a->c,indexes)==1 && atAction(a,syms,0) && atAction(b,syms,1)) break;
        frames(a,b,1);
    }
    screenshot(a,"multiplayer/tests/duel-switched.png");
    assert(a->c->busRead16(a->c,indexes)==1);
    assert(b->c->busRead16(b->c,indexes)==1);
    assert(atAction(a,syms,0) && atAction(b,syms,1));
    puts("PASS: native party menu switches to the owner's second Pokemon on both screens");
    unsigned cursor=symbol(syms,"gActionSelectionCursor");
    if(!(a->c->busRead8(a->c,cursor)&2)) tap(a,a,b,7,10);
    if(!(a->c->busRead8(a->c,cursor)&1)) tap(a,a,b,4,10);
    assert(a->c->busRead8(a->c,cursor)==3);
    tap(a,a,b,0,60); // RUN: forfeit
    for(unsigned n=0;n<4000;n++) {
        if(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4) break;
        a->c->setKeys(a->c,n%30==0 ? 1:0); b->c->setKeys(b->c,n%30==0 ? 1:0); frames(a,b,1);
    }
    a->c->setKeys(a->c,0); b->c->setKeys(b->c,0); frames(a,b,180);
    report(a,m); report(b,m);
    assert(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4);
    assert(!a->c->busRead8(a->c,m+78) && !b->c->busRead8(b->c,m+78));
    assert(a->c->busRead8(a->c,m+77)==2 && b->c->busRead8(b->c,m+77)==1);
    for(unsigned i=0;i<600;i++) { assert(before[0][i]==a->c->busRead8(a->c,party+i)); assert(before[1][i]==b->c->busRead8(b->c,party+i)); }
    closeInvitation(a,b);
    puts("PASS: repeated duel gets a fresh session; RUN forfeits and restores both teams");

    if(b->c->busRead8(b->c,m+19)!=4) tap(b,a,b,4,30);
    beginInvitation(b,a,m); tap(a,a,b,0,180); awaitBattle(a,b,m,syms);
    puts("PASS: guest can initiate a duel; link player roles remain consistent");
    runCode(b,"FRMP_STOP()"); frames(a,b,600);
    report(a,m); report(b,m);
    assert(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4);
    assert(a->c->busRead8(a->c,m+78)==2 && b->c->busRead8(b->c,m+78)==2);
    for(unsigned i=0;i<600;i++) { assert(before[0][i]==a->c->busRead8(a->c,party+i)); assert(before[1][i]==b->c->busRead8(b->c,party+i)); }
    screenshot(a,"multiplayer/tests/duel-disconnected.png");
    closeInvitation(a,b);
    assert(mScriptContextLoadFile(&b->ctx,clientScript));
    struct mScriptEngineContext *engine=HashTableLookup(&b->ctx.engines,"lua");
    assert(engine->run(engine)); frames(a,b,180);
    assert(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4);
    assert(a->c->busRead8(a->c,m+13)<16 && b->c->busRead8(b->c,m+13)<16);
    puts("PASS: disconnect during battle restores both teams and returns safely; reconnect resumes exploration");

    beginInvitation(a,b,m); tap(b,a,b,0,0);
    for(unsigned n=0;n<120;n++) {
        if(a->c->busRead8(a->c,m+74)==1 && b->c->busRead8(b->c,m+74)==1) break;
        frames(a,b,1);
    }
    assert(a->c->busRead8(a->c,m+74)==1 && b->c->busRead8(b->c,m+74)==1);
    runCode(b,"FRMP_STOP()"); frames(a,b,600);
    assert(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4);
    assert(a->c->busRead8(a->c,m+78)==2 && b->c->busRead8(b->c,m+78)==2);
    for(unsigned i=0;i<600;i++) { assert(before[0][i]==a->c->busRead8(a->c,party+i)); assert(before[1][i]==b->c->busRead8(b->c,party+i)); }
    closeInvitation(a,b);
    assert(mScriptContextLoadFile(&b->ctx,clientScript)); assert(engine->run(engine)); frames(a,b,180);
    assert(a->c->busRead8(a->c,m+13)<16 && b->c->busRead8(b->c,m+13)<16);
    puts("PASS: disconnect during entry fade returns to map before battle allocation");
}
static void callFieldFunction(struct Instance *who,struct Instance *a,struct Instance *b,const char *syms,const char *function) {
    // One-shot Thumb trampoline in the unused transport buffer. It restores
    // CB2 before tail-calling the real field function, so it cannot run twice.
    struct mCore *c=who->c;
    unsigned main=symbol(syms,"gMain"),code=symbol(syms,"gBlockSendBuffer");
    unsigned short instructions[]={0x4802,0x4903,0x6048,0x4803,0x4700,0x46c0};
    for(unsigned j=0;j<6;j++) c->busWrite16(c,code+j*2,instructions[j]);
    c->busWrite32(c,code+12,c->busRead32(c,main+4));
    c->busWrite32(c,code+16,main);
    c->busWrite32(c,code+20,symbol(syms,function)|1);
    c->busWrite32(c,main+4,code|1);
    frames(a,b,160);
}

static void coopTrial(struct Instance *a,struct Instance *b,unsigned m,const char *syms,unsigned opponents,unsigned recovery,const char *clientScript) {
    fprintf(stderr,"BEGIN COOP opponents=%u recovery=%u\n", opponents, recovery);
    struct Instance *players[2]={a,b};
    unsigned capture=recovery==7 || recovery==8;
    unsigned medicine=recovery==9 || recovery==10;
    unsigned field=(recovery>=3 && recovery<=5) || recovery==12 || capture || medicine,owner=(recovery==4 || recovery==8 || recovery==9) ? 1:0;
    unsigned party=symbol(syms,"gPlayerParty"),enemy=symbol(syms,"gEnemyParty");
    unsigned count=symbol(syms,"gPlayerPartyCount");
    unsigned char original[2][600],counts[2];
    unsigned char story[2][0x320]; unsigned money[2];
    unsigned ptr1=symbol(syms,"gSaveBlock1Ptr"),ptr2=symbol(syms,"gSaveBlock2Ptr");
    for(unsigned p=0;p<2;p++) {
        struct mCore *c=players[p]->c;
        // Independent encounter fixtures start healed; combat changes are not reset by the game.
        if(capture) {
            c->busWrite8(c,count,recovery==8 ? 6:1);
            for(unsigned slot=1;slot<6;slot++)
                for(unsigned j=0;j<100;j++) c->busWrite8(c,party+slot*100+j,recovery==8 ? c->busRead8(c,party+j):0);
            if(p==owner) {
                unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
                c->busWrite16(c,save+0x430,1); // Three Master Balls, fixture only.
                c->busWrite16(c,save+0x432,3^(key&65535));
                c->busWrite16(c,symbol(syms,"gBagMenuState")+6,2);
            }
        }
        if(field)
            for(unsigned slot=0;slot<c->busRead8(c,count);slot++)
                c->busWrite16(c,party+slot*100+86,c->busRead16(c,party+slot*100+88));
        if(medicine) {
            unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
            c->busWrite16(c,save+0x310,13); // Potion, three per player.
            c->busWrite16(c,save+0x312,3^(key&65535));
            c->busWrite16(c,symbol(syms,"gBagMenuState")+6,0);
            if(p==owner) c->busWrite16(c,party+86,c->busRead16(c,party+88)-5);
        }
        if(recovery==6) {
            // Six usable reserves per owner, with distinct species between owners.
            for(unsigned slot=1;slot<6;slot++)
                for(unsigned j=0;j<100;j++) c->busWrite8(c,party+slot*100+j,c->busRead8(c,party+j));
            c->busWrite8(c,count,6);
        }
        unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
        money[p]=c->busRead32(c,save+0x290)^key;
        for(unsigned j=0;j<0x320;j++) story[p][j]=c->busRead8(c,save+0xee0+j);
        counts[p]=c->busRead8(c,count);
        for(unsigned j=0;j<600;j++) original[p][j]=c->busRead8(c,party+j);
    }
    // Create the encounter through FireRed's own CreateMon routine, only in the fixture.
    struct mCore *encounter=players[owner]->c;
    for(unsigned j=0;j<600;j++) encounter->busWrite8(encounter,enemy+j,0);
    seedParty(players[owner],a,b,syms,19); // Rattata, level 5
    if(opponents==2)
        for(unsigned j=0;j<100;j++) encounter->busWrite8(encounter,enemy+100+j,encounter->busRead8(encounter,enemy+j));
    if(recovery==6)
        // Empty reserve slots stay empty, but their unused padding identifies
        // the later transport chunks without inventing extra wild opponents.
        for(unsigned slot=2;slot<6;slot++) encounter->busWrite8(encounter,enemy+slot*100+30,0x50+slot);
    unsigned char expectedEnemy[600];
    for(unsigned j=0;j<600;j++) expectedEnemy[j]=encounter->busRead8(encounter,enemy+j);
    for(unsigned j=0;j<600;j++) encounter->busWrite8(encounter,party+j,original[owner][j]);
    encounter->busWrite8(encounter,count,counts[owner]);
    for(unsigned p=0;p<2;p++) {
        struct mCore *c=players[p]->c;
        // Arm the development-only mode. Invitations and combat still use game input.
        c->busWrite8(c,symbol(syms,"sCoopTrial"),field || (recovery==2 && p==1) ? 0:1);
    }
    if(field) {
        encounter->busWrite8(encounter,symbol(syms,"sCoopFieldEnabled"),1);
        callFieldFunction(players[owner],a,b,syms,"StartWildBattle");
        report(a,m); report(b,m);
        assert(encounter->busRead8(encounter,m+52)==1);
        assert(encounter->busRead32(encounter,m+44)&0x40000000);
        tap(players[owner],a,b,0,160);
        assert(players[owner^1]->c->busRead8(players[owner^1]->c,m+52)==3);
        screenshot(players[owner^1],"multiplayer/tests/coop-field-invite.png");
        if (recovery==12) {
            tap(players[owner],a,b,0,120); // Choose YES and enter the waiting screen.
            assert(encounter->busRead8(encounter,m+52)==2);
            tap(players[owner],a,b,1,120); // B cancels while the peer has not answered.
            assert(encounter->busRead8(encounter,m+52)==9);
            assert(!encounter->busRead8(encounter,m+74));
            assert(!(encounter->busRead32(encounter,symbol(syms,"gBattleTypeFlags"))&2));
            assert(encounter->busRead8(encounter,symbol(syms,"gBattlersCount"))==2);
            puts("PASS: B cancels cooperative wait and resumes the wild battle as solo");
            return;
        }
        tap(players[owner^1],a,b,recovery==5 ? 1:0,180);
        if(recovery==5) {
            assert(encounter->busRead8(encounter,m+52)==8);
            tap(players[owner],a,b,0,120); // Close refusal; original solo encounter resumes.
            unsigned wait;
            // Solo introduction waits for A, unlike the link battle introduction.
            for(wait=0;wait<4000 && !atAction(a,syms,0);wait++) {
                a->c->setKeys(a->c,wait%30==0 ? 1:0);
                frames(a,b,1);
            }
            a->c->setKeys(a->c,0);
            if(wait==4000) screenshot(a,"multiplayer/tests/coop-declined-failed.png");
            assert(wait<4000);
            assert(!(encounter->busRead32(encounter,symbol(syms,"gBattleTypeFlags"))&2));
            assert(encounter->busRead8(encounter,symbol(syms,"gBattlersCount"))==2);
            for(unsigned j=0;j<100;j++) assert(encounter->busRead8(encounter,enemy+j)==expectedEnemy[j]);
            assert(b->c->busRead8(b->c,m+16));
            screenshot(a,"multiplayer/tests/coop-declined-solo.png");
            for(wait=0;wait<5000;wait++) {
                if(encounter->busRead8(encounter,m+16) && encounter->busRead8(encounter,m+13)<16) break;
                if(wait%30==0 && atAction(a,syms,0)) {
                    unsigned cursor=encounter->busRead8(encounter,symbol(syms,"gActionSelectionCursor"));
                    if(!(cursor&2)) tap(a,a,b,7,20);
                    if(!(cursor&1)) tap(a,a,b,4,20);
                    tap(a,a,b,0,0);
                } else {
                    a->c->setKeys(a->c,wait%30==0 ? 1:0); frames(a,b,1);
                }
            }
            a->c->setKeys(a->c,0); frames(a,b,60);
            assert(wait<5000);
            for(unsigned j=0;j<600;j++) assert(b->c->busRead8(b->c,party+j)==original[1][j]);
            puts("PASS: refusing cooperation resumes the same native solo encounter; guest stays in the field; RUN returns normally");
            return;
        }
    } else {
        beginInvitation(a,b,m); tap(b,a,b,0,180);
    }
    unsigned n;
    if(recovery==2) {
        frames(a,b,360);
        for(unsigned p=0;p<2;p++) {
            assert(players[p]->c->busRead8(players[p]->c,m+74)==4);
            assert(players[p]->c->busRead8(players[p]->c,m+78)==3);
        }
        puts("PASS: mismatched cooperative/PvP modes abort before entering combat");
        goto checkRestored;
    }
    for(n=0;n<4000;n++) {
        if(atAction(a,syms,0) && atAction(b,syms,2)) break;
        frames(a,b,1);
    }
    frames(a,b,30); report(a,m); report(b,m);
    screenshot(a,opponents==1 ? "multiplayer/tests/coop-host.png":"multiplayer/tests/coop-2v2-host.png");
    screenshot(b,opponents==1 ? "multiplayer/tests/coop-client.png":"multiplayer/tests/coop-2v2-client.png");
    assert(n<4000);
    for(unsigned p=0;p<2;p++)
        for(unsigned j=0;j<600;j++) assert(players[p]->c->busRead8(players[p]->c,enemy+j)==expectedEnemy[j]);
    if(field) {
        assert((a->c->busRead32(a->c,m+56)&1)==owner);
        printf("PASS: real wild-battle entry invites cooperation; encounter belongs to %s independently of TCP host\n",owner ? "client":"host");
    }
    assert(a->c->busRead8(a->c,symbol(syms,"gBattlersCount"))==4);
    assert(a->c->busRead8(a->c,symbol(syms,"gAbsentBattlerFlags"))==(opponents==1 ? 8:0));
    for(unsigned p=0;p<2;p++) {
        struct mCore *c=players[p]->c;
        for(unsigned bank=0;bank<4;bank++) {
            unsigned sprite=c->busRead8(c,symbol(syms,"gHealthboxSpriteIds")+bank);
            unsigned flags=c->busRead8(c,symbol(syms,"gSprites")+68*sprite+0x3e);
            unsigned addr=symbol(syms,"gSprites")+68*sprite;
            assert(flags&1);
            if(bank!=3 || opponents==2) {
                short x=c->busRead16(c,addr+0x20),y=c->busRead16(c,addr+0x22);
                assert(!(flags&4) && x>=0 && x<240 && y>=0 && y<112);
                assert(!(c->busRead16(c,addr)&0x0c00)); // visible OAM, not a VS-letter mask
            }
        }
    }
    for(unsigned p=0;p<2;p++)
        for(unsigned j=0;j<600;j++) {
            assert(players[p]->c->busRead8(players[p]->c,party+j)==original[p][j]);
            assert(players[p]->c->busRead8(players[p]->c,symbol(syms,"sRemoteParty")+j)==original[p^1][j]);
        }
    printf("PASS: cooperative 2v%u trial has separate human controllers, native AI and visible healthboxes\n",opponents);
    if(medicine) {
        struct Instance *user=players[owner];
        tap(user,a,b,4,20); tap(user,a,b,0,180);
        tap(user,a,b,0,90); tap(user,a,b,0,180); // Potion, USE, own party.
        tap(user,a,b,0,120); tap(user,a,b,0,240); // Select active Pokemon, close message.
        screenshot(user,"multiplayer/tests/coop-potion.png");
        for(n=0;n<1200;n++) {
            unsigned max=user->c->busRead16(user->c,party+88),mons=symbol(syms,"gBattleMons")+owner*2*88+0x28;
            if(a->c->busRead16(a->c,mons)==max && b->c->busRead16(b->c,mons)==max) break;
            user->c->setKeys(user->c,n%30==0 ? 1:0); frames(a,b,1);
        }
        user->c->setKeys(user->c,0); frames(a,b,90);
        if(n==1200) for(unsigned p=0;p<2;p++) {
            struct mCore *c=players[p]->c;
            fprintf(stderr,"medicine owner=%u core=%u hp=%u partyhp=%u choice=%u/%u/%u/%u comm=%u exec=%08x\n",owner,p,
                c->busRead16(c,symbol(syms,"gBattleMons")+owner*2*88+0x28),c->busRead16(c,party+86),
                c->busRead8(c,symbol(syms,"gBattleBufferB")+owner*2*512+1),c->busRead8(c,symbol(syms,"gBattleBufferB")+owner*2*512+3),
                c->busRead8(c,symbol(syms,"gBattleBufferB")+owner*2*512+4),c->busRead8(c,symbol(syms,"gBattleBufferB")+owner*2*512+5),
                c->busRead8(c,symbol(syms,"gBattleCommunication")+owner*2),c->busRead32(c,symbol(syms,"gBattleControllerExecFlags")));
            fprintf(stderr,"callback=%08x ctrl=%08x command=%u chosen=%u slot=%u phase=%u tx=%u ack=%u\n",
                c->busRead32(c,symbol(syms,"gMain")+4),c->busRead32(c,symbol(syms,"gBattlerControllerFuncs")+owner*8),
                c->busRead8(c,symbol(syms,"gBattleBufferA")+owner*1024),c->busRead16(c,symbol(syms,"sChosenItem")),
                c->busRead8(c,symbol(syms,"sChosenPartyIndex")),c->busRead8(c,m+74),c->busRead32(c,m+64),c->busRead32(c,m+68));
        }
        assert(n<1200);
        unsigned remote=symbol(syms,"sRemoteParty");
        assert(a->c->busRead16(a->c,(owner ? remote:party)+86)==user->c->busRead16(user->c,party+88));
        for(unsigned p=0;p<2;p++) {
            struct mCore *c=players[p]->c;
            assert(c->busRead16(c,symbol(syms,"gBattleMons")+owner*2*88+0x28)==user->c->busRead16(user->c,party+88));
            assert(c->busRead16(c,(p==owner ? party:remote)+86)==user->c->busRead16(user->c,party+88));
        }
        for(unsigned p=0;p<2;p++) {
            struct mCore *c=players[p]->c;
            unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
            assert((c->busRead16(c,save+0x312)^(key&65535))==(p==owner ? 2:3));
        }
        for(unsigned j=0;j<600;j++) assert(players[owner^1]->c->busRead8(players[owner^1]->c,party+j)==original[owner^1][j]);
        printf("PASS: %s uses Potion through native Bag; host sees healed own Pokemon; partner party and inventory unchanged\n",owner ? "client":"host");
        if(recovery==9) {
        runCode(b,"FRMP_STOP()"); frames(a,b,600);
        for(unsigned p=0;p<2;p++) {
            struct mCore *c=players[p]->c;
            unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
            assert(c->busRead8(c,m+74)==4);
            assert((c->busRead16(c,save+0x312)^(key&65535))==3);
            for(unsigned j=0;j<600;j++) assert(c->busRead8(c,party+j)==original[p][j]);
        }
        puts("PASS: disconnect after medicine restores both teams and refunds only the consumed inventory");
        assert(mScriptContextLoadFile(&b->ctx,clientScript));
        struct mScriptEngineContext *engine=HashTableLookup(&b->ctx.engines,"lua");
        assert(engine->run(engine));
        closeInvitation(a,b); frames(a,b,180);
        return;
        }
    }
    if(capture) {
        struct Instance *catcher=players[owner],*friend=players[owner^1];
        tap(catcher,a,b,4,20); tap(catcher,a,b,0,180); // BAG, Balls pocket.
        screenshot(catcher,"multiplayer/tests/coop-capture-bag.png");
        tap(catcher,a,b,0,90); tap(catcher,a,b,0,240); // Select Master Ball, USE.
        tap(friend,a,b,0,90); tap(friend,a,b,0,90); tap(friend,a,b,0,90);
        for(n=0;n<8000;n++) {
            if(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4) break;
            a->c->setKeys(a->c,n%30==0 ? 1:0); b->c->setKeys(b->c,n%30==0 ? 1:0);
            frames(a,b,1);
        }
        a->c->setKeys(a->c,0); b->c->setKeys(b->c,0); frames(a,b,180);
        screenshot(a,"multiplayer/tests/coop-capture-host.png");
        screenshot(b,"multiplayer/tests/coop-capture-client.png");
        report(a,m); report(b,m);
        if(n==8000) for(unsigned p=0;p<2;p++) {
            struct mCore *c=players[p]->c;
            fprintf(stderr,"capture stuck p=%u cb2=%08x main=%08x exec=%08x script=%08x outcome=%u controllers=",p,
                c->busRead32(c,symbol(syms,"gMain")+4),c->busRead32(c,symbol(syms,"gBattleMainFunc")),
                c->busRead32(c,symbol(syms,"gBattleControllerExecFlags")),c->busRead32(c,symbol(syms,"gBattlescriptCurrInstr")),
                c->busRead8(c,symbol(syms,"gBattleOutcome")));
            for(unsigned bank=0;bank<4;bank++) fprintf(stderr,"%08x ",c->busRead32(c,symbol(syms,"gBattlerControllerFuncs")+bank*4));
            fprintf(stderr,"\n");
        }
        assert(n<8000);
        for(unsigned p=0;p<2;p++) {
            assert(!players[p]->c->busRead8(players[p]->c,m+78));
            assert(players[p]->c->busRead8(players[p]->c,m+77)==7);
        }
        struct mCore *c=catcher->c;
        unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
        assert((c->busRead16(c,save+0x432)^(key&65535))==2);
        assert(c->busRead8(c,count)==(recovery==8 ? 6:2));
        assert(friend->c->busRead8(friend->c,count)==counts[owner^1]);
        unsigned caught=recovery==8 ? c->busRead32(c,symbol(syms,"gPokemonStoragePtr"))+4 : party+100;
        for(unsigned j=0;j<4;j++) assert(c->busRead8(c,caught+j)==expectedEnemy[j]);
        for(unsigned j=0;j<4;j++) assert(c->busRead8(c,caught+4+j)==c->busRead8(c,c->busRead32(c,ptr2)+10+j));
        for(unsigned j=0;j<600;j++) assert(friend->c->busRead8(friend->c,party+j)==original[owner^1][j]);
        printf("PASS: %s catches through the native Bag; only catcher consumes a ball and receives Pokemon in %s\n",owner ? "client":"host",recovery==8 ? "PC storage":"own party");
        closeInvitation(a,b);
        return;
    }
    if(recovery==6) {
        for(unsigned p=0;p<2;p++) {
            tap(players[p],a,b,7,20); tap(players[p],a,b,0,180);
            screenshot(players[p],p ? "multiplayer/tests/coop-party-client.png":"multiplayer/tests/coop-party-host.png");
            for(unsigned slot=0;slot<5;slot++) tap(players[p],a,b,7,20);
            tap(players[p],a,b,0,60); tap(players[p],a,b,0,180);
        }
        unsigned indexes=symbol(syms,"gBattlerPartyIndexes");
        for(n=0;n<4000;n++) {
            if(atAction(a,syms,0) && atAction(b,syms,2)) break;
            frames(a,b,1);
        }
        screenshot(a,"multiplayer/tests/coop-reserves-host.png");
        screenshot(b,"multiplayer/tests/coop-reserves-client.png");
        assert(n<4000);
        for(unsigned p=0;p<2;p++) {
            assert(players[p]->c->busRead16(players[p]->c,indexes)==5);
            assert(players[p]->c->busRead16(players[p]->c,indexes+4)==5);
        }
        puts("PASS: both humans select their own sixth Pokemon simultaneously; identical slot numbers do not conflict");
    }
    unsigned hp=a->c->busRead16(a->c,enemy+86);
    tap(a,a,b,0,90); tap(a,a,b,0,90); tap(a,a,b,0,90);
    assert(atAction(b,syms,2));
    assert(a->c->busRead16(a->c,enemy+86)==hp);
    puts("PASS: cooperative turn waits for the other human; host cannot choose partner action");
    if(recovery==1) {
        runCode(b,"FRMP_STOP()"); frames(a,b,600);
        for(unsigned p=0;p<2;p++) {
            assert(players[p]->c->busRead8(players[p]->c,m+74)==4);
            assert(players[p]->c->busRead8(players[p]->c,m+78)==2);
        }
        puts("PASS: disconnect during a cooperative turn aborts both battles");
        goto checkRestored;
    }
    int damage=0;
    for(n=0;n<18000;n++) {
        if(medicine) for(unsigned p=0;p<2;p++) if(atAction(players[p],syms,p*2)) {
            unsigned cursor=players[p]->c->busRead8(players[p]->c,symbol(syms,"gActionSelectionCursor")+p*2);
            if(cursor&1) tap(players[p],a,b,5,10);
            if(cursor&2) tap(players[p],a,b,6,10);
        }
        if(a->c->busRead8(a->c,m+74)==4 && b->c->busRead8(b->c,m+74)==4) break;
        a->c->setKeys(a->c,n%30==0 ? 1:0); b->c->setKeys(b->c,n%30==0 ? 1:0);
        frames(a,b,1);
        if(a->c->busRead8(a->c,m+74)==2 && a->c->busRead16(a->c,enemy+86)<hp) damage=1;
    }
    a->c->setKeys(a->c,0); b->c->setKeys(b->c,0); frames(a,b,180);
    report(a,m); report(b,m);
    screenshot(a,"multiplayer/tests/coop-return.png");
    assert(damage && n<18000);
    assert(!a->c->busRead8(a->c,m+78) && !b->c->busRead8(b->c,m+78));
    assert(a->c->busRead8(a->c,m+77)==b->c->busRead8(b->c,m+77));
    assert(a->c->busRead8(a->c,m+77)==1);
checkRestored:
    for(unsigned p=0;p<2;p++) {
        struct mCore *c=players[p]->c;
        unsigned save=c->busRead32(c,ptr1),key=c->busRead32(c,c->busRead32(c,ptr2)+0xf20);
        assert(c->busRead8(c,count)==counts[p]);
        if(field) {
            unsigned char current[600];
            for(unsigned j=0;j<600;j++) current[j]=c->busRead8(c,party+j);
            fprintf(stderr,"reward owner=%u exp=%u -> %u hp=%u level=%u\n",p,
                monExperience(original[p]),monExperience(current),current[86]|current[87]<<8,current[84]);
            assert(monExperience(current)>monExperience(original[p]));
            assert(!memcmp(current,original[p],8)); // Personality and OT stay with their owner.
            for(unsigned j=100;j<600;j++) assert(current[j]==original[p][j]);
        } else
            for(unsigned j=0;j<600;j++) assert(c->busRead8(c,party+j)==original[p][j]);
        assert((c->busRead32(c,save+0x290)^key)==money[p]);
        if(medicine) assert((c->busRead16(c,save+0x312)^(key&65535))==(p==owner ? 2:3));
        for(unsigned j=0;j<0x320;j++) assert(c->busRead8(c,save+0xee0+j)==story[p][j]);
    }
    if(field) puts("PASS: each owner retains battle changes and earns individual EXP with valid Pokemon checksums; campaigns stay independent");
    else if(!recovery || recovery==6) puts("PASS: cooperative AI battle resolves with shared victory; original teams and both campaigns remain intact");
    else puts("PASS: aborted cooperative preparation/battle restores teams and preserves both campaigns");
    closeInvitation(a,b);
    if(recovery==1) {
        assert(mScriptContextLoadFile(&b->ctx,clientScript));
        struct mScriptEngineContext *engine=HashTableLookup(&b->ctx.engines,"lua");
        assert(engine->run(engine)); frames(a,b,180);
        assert(a->c->busRead8(a->c,m+13)<16 && b->c->busRead8(b->c,m+13)<16);
        puts("PASS: reconnect after cooperative battle returns both visitors without resuming old combat");
    }
}

int main(int argc,char **argv) {
    assert(argc==5); // ROM host.lua client.lua symbols
    struct Instance *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));
    unsigned m=symbol(argv[4],"gMultiplayer"),mainAddr=symbol(argv[4],"gMain");
    unsigned newGame=symbol(argv[4],"CB2_NewGame");
    init(a,argv[1],argv[2]); init(b,argv[1],argv[3]);
    frames(a,b,600);
    // Test-only bootstrap into a NEW game. No user save is loaded or written.
    unsigned storagePtr=symbol(argv[4],"gPokemonStoragePtr"),storage=symbol(argv[4],"gPokemonStorage");
    a->c->busWrite32(a->c,storagePtr,storage); b->c->busWrite32(b->c,storagePtr,storage);
    unsigned sav1=symbol(argv[4],"gSaveBlock1"),sav2=symbol(argv[4],"gSaveBlock2");
    unsigned ptr1=symbol(argv[4],"gSaveBlock1Ptr"),ptr2=symbol(argv[4],"gSaveBlock2Ptr");
    a->c->busWrite32(a->c,ptr1,sav1); b->c->busWrite32(b->c,ptr1,sav1);
    a->c->busWrite32(a->c,ptr2,sav2); b->c->busWrite32(b->c,ptr2,sav2);
    for (int n=0;n<8;n++) {
        a->c->busWrite8(a->c,sav1+0x3a4c+n,0xff); b->c->busWrite8(b->c,sav1+0x3a4c+n,0xff);
        a->c->busWrite8(a->c,sav2+n,0xff); b->c->busWrite8(b->c,sav2+n,0xff);
    }
    a->c->busWrite32(a->c,mainAddr,0); b->c->busWrite32(b->c,mainAddr,0);
    a->c->busWrite8(a->c,mainAddr+0x438,0); b->c->busWrite8(b->c,mainAddr+0x438,0);
    a->c->busWrite32(a->c,mainAddr+4,newGame|1); b->c->busWrite32(b->c,mainAddr+4,newGame|1);
    frames(a,b,600);
    a->c->setKeys(a->c,1<<4); frames(a,b,16); a->c->setKeys(a->c,0);
    frames(a,b,180);
    report(a,m); report(b,m);
    screenshot(a,"multiplayer/tests/host.png"); screenshot(b,"multiplayer/tests/client.png");
    assert(a->c->busRead32(a->c,m)==0x504d5246 && b->c->busRead32(b->c,m)==0x504d5246);
    assert(a->c->busRead8(a->c,m+12)==1 && b->c->busRead8(b->c,m+12)==1);
    assert(a->c->busRead8(a->c,m+16)==1 && b->c->busRead8(b->c,m+16)==1);
    assert(a->c->busRead8(a->c,m+13)<16 && b->c->busRead8(b->c,m+13)<16);
    assert(a->c->busRead16(a->c,m+20)==b->c->busRead16(b->c,m+32));
    assert(b->c->busRead16(b->c,m+20)==a->c->busRead16(a->c,m+32));
    assert(a->c->busRead16(a->c,m+20)!=b->c->busRead16(b->c,m+20));
    puts("PASS: two real mGBA cores, Lua TCP handshake, separate positions and remote objects");
    // Select the game's FAST text option so input timing does not depend on
    // the uninitialized defaults in the test-only new-game bootstrap.
    a->c->busWrite8(a->c,a->c->busRead32(a->c,ptr2)+0x14,2);
    b->c->busWrite8(b->c,b->c->busRead32(b->c,ptr2)+0x14,2);
    tap(a,a,b,5,30); // face the visitor
    tap(a,a,b,0,180); // new games have no Pokemon yet
    assert(!a->c->busRead8(a->c,m+74) && b->c->busRead8(b->c,m+52)==0);
    tap(a,a,b,0,60);
    assert(a->c->busRead8(a->c,m+52)==9);
    puts("PASS: empty party is rejected before sending a duel invitation");
    seedParty(a,a,b,argv[4],25);
    tap(a,a,b,0,160); tap(a,a,b,0,180);
    assert(a->c->busRead8(a->c,m+52)==8 && b->c->busRead8(b->c,m+52)==8);
    assert(!a->c->busRead8(a->c,m+74) && !b->c->busRead8(b->c,m+74));
    closeInvitation(a,b);
    puts("PASS: recipient without a usable Pokemon refuses before entering battle");
    seedParty(b,a,b,argv[4],7);
    // Real in-game A/B input drives the scripts and the two-sided handshake.
    if(getenv("FRMP_COOP_ONLY")) {
        if(getenv("FRMP_EXP_ONLY")) {
            coopTrial(a,b,m,argv[4],1,3,argv[3]);
            return 0;
        }
        if(getenv("FRMP_CANCEL_COOP_ONLY")) {
            coopTrial(a,b,m,argv[4],1,12,argv[3]);
            return 0;
        }
        if(getenv("FRMP_RESERVES_ONLY")) {
            coopTrial(a,b,m,argv[4],1,6,argv[3]);
            return 0;
        }
        if(getenv("FRMP_CAPTURE_ONLY")) {
            coopTrial(a,b,m,argv[4],1,7,argv[3]);
            coopTrial(a,b,m,argv[4],1,8,argv[3]);
            return 0;
        }
        if(getenv("FRMP_ITEMS_ONLY")) {
            coopTrial(a,b,m,argv[4],1,9,argv[3]);
            coopTrial(a,b,m,argv[4],1,10,argv[3]);
            return 0;
        }
        coopTrial(a,b,m,argv[4],1,0,argv[3]); coopTrial(a,b,m,argv[4],2,0,argv[3]);
        coopTrial(a,b,m,argv[4],1,1,argv[3]); coopTrial(a,b,m,argv[4],1,2,argv[3]);
        coopTrial(a,b,m,argv[4],1,3,argv[3]); coopTrial(a,b,m,argv[4],1,4,argv[3]);
        coopTrial(a,b,m,argv[4],1,5,argv[3]);
        coopTrial(a,b,m,argv[4],1,6,argv[3]);
        coopTrial(a,b,m,argv[4],1,7,argv[3]); coopTrial(a,b,m,argv[4],1,8,argv[3]);
        coopTrial(a,b,m,argv[4],1,9,argv[3]); coopTrial(a,b,m,argv[4],1,10,argv[3]);
        goto cleanup;
    }
    playDuel(a,b,m,argv[4]);
    seedBench(a,a,b,argv[4],1); seedBench(b,a,b,argv[4],4);
    duelRecoveryTests(a,b,m,argv[4],argv[3]);
    coopTrial(a,b,m,argv[4],1,0,argv[3]);
    coopTrial(a,b,m,argv[4],2,0,argv[3]);
    coopTrial(a,b,m,argv[4],1,1,argv[3]);
    coopTrial(a,b,m,argv[4],1,2,argv[3]);
    coopTrial(a,b,m,argv[4],1,3,argv[3]);
    coopTrial(a,b,m,argv[4],1,4,argv[3]);
    coopTrial(a,b,m,argv[4],1,5,argv[3]);
    coopTrial(a,b,m,argv[4],1,6,argv[3]);
    coopTrial(a,b,m,argv[4],1,7,argv[3]); coopTrial(a,b,m,argv[4],1,8,argv[3]);
    coopTrial(a,b,m,argv[4],1,9,argv[3]); coopTrial(a,b,m,argv[4],1,10,argv[3]);
    beginInvitation(a,b,m);
    tap(b,a,b,1,180); // B refuses the YES/NO prompt
    assert(a->c->busRead8(a->c,m+52)==8 && b->c->busRead8(b->c,m+52)==8);
    assert(a->c->busRead8(a->c,m+55)==1);
    tap(a,a,b,0,60);
    puts("PASS: guest refusal is delivered and both players regain control");
    beginInvitation(a,b,m);
    tap(a,a,b,1,80); // sender cancels while the guest is still choosing
    tap(b,a,b,0,180); // late YES must not resurrect a cancelled invitation
    assert(a->c->busRead8(a->c,m+52)==9 && b->c->busRead8(b->c,m+52)==9);
    assert(a->c->busRead8(a->c,m+55)==0 && b->c->busRead8(b->c,m+55)==0);
    closeInvitation(a,b);
    puts("PASS: B cancels; a late acceptance cannot revive the invitation");
    beginInvitation(a,b,m);
    frames(a,b,1810);
    tap(b,a,b,0,180);
    assert(a->c->busRead8(a->c,m+52)==9 && b->c->busRead8(b->c,m+52)==9);
    closeInvitation(a,b);
    puts("PASS: unanswered invitation expires without starting a battle");
    if (b->c->busRead8(b->c,m+19)!=4) tap(b,a,b,4,30); // turn, do not step
    a->c->setKeys(a->c,1); b->c->setKeys(b->c,1); frames(a,b,1);
    a->c->setKeys(a->c,0); b->c->setKeys(b->c,0); frames(a,b,160);
    report(a,m); report(b,m);
    assert(a->c->busRead8(a->c,m+52)==9 && b->c->busRead8(b->c,m+52)==9);
    assert(a->c->busRead32(a->c,m+44)!=b->c->busRead32(b->c,m+44));
    tap(a,a,b,0,160); tap(b,a,b,0,160);
    assert(a->c->busRead8(a->c,m+55)==0 && b->c->busRead8(b->c,m+55)==0);
    closeInvitation(a,b);
    puts("PASS: simultaneous interaction uses distinct IDs and cancels without implicit consent");
    beginInvitation(a,b,m);
    runCode(b,"FRMP_STOP()"); frames(a,b,360);
    tap(b,a,b,0,180);
    assert(a->c->busRead8(a->c,m+52)==9 && b->c->busRead8(b->c,m+52)==9);
    assert(!a->c->busRead8(a->c,m+12) && !b->c->busRead8(b->c,m+12));
    closeInvitation(a,b);
    assert(mScriptContextLoadFile(&b->ctx,argv[3]));
    struct mScriptEngineContext *reconnectEngine=HashTableLookup(&b->ctx.engines,"lua");
    assert(reconnectEngine->run(reconnectEngine)); frames(a,b,180);
    assert(a->c->busRead8(a->c,m+52)==9 && b->c->busRead8(b->c,m+52)==9);
    puts("PASS: disconnect invalidates pending invitation; reconnect does not resurrect it");
    // Exercise the real save serialization function in RAM; no flash save is written.
    unsigned old1=a->c->busRead32(a->c,mainAddr),old2=a->c->busRead32(a->c,mainAddr+4);
    unsigned remoteId=a->c->busRead8(a->c,m+13);
    a->c->busWrite32(a->c,mainAddr,0);
    a->c->busWrite32(a->c,mainAddr+4,symbol(argv[4],"SaveObjectEvents")|1);
    frames(a,b,3);
    unsigned saveBlock=a->c->busRead32(a->c,ptr1);
    assert(!(a->c->busRead8(a->c,saveBlock+0x6a0+remoteId*0x24)&1));
    a->c->busWrite32(a->c,mainAddr,old1); a->c->busWrite32(a->c,mainAddr+4,old2);
    frames(a,b,30);
    assert(a->c->busRead8(a->c,m+13)<16);
    puts("PASS: save serialization excludes transient visitor without removing live player");
    walkTo(a,b,m,14,11);
    walkTo(a,b,m,17,11);
    a->c->setKeys(a->c,1<<6); frames(a,b,60); a->c->setKeys(a->c,0);
    // FireRed stair warps activate on horizontal input while standing on the warp.
    a->c->setKeys(a->c,1<<5); frames(a,b,24); a->c->setKeys(a->c,0);
    frames(a,b,240);
    report(a,m); report(b,m);
    screenshot(a,"multiplayer/tests/host-other-map.png");
    assert(a->c->busRead8(a->c,m+17)!=b->c->busRead8(b->c,m+17)
        || a->c->busRead8(a->c,m+18)!=b->c->busRead8(b->c,m+18));
    assert(a->c->busRead8(a->c,m+13)==16 && b->c->busRead8(b->c,m+13)==16);
    puts("PASS: real stair transition; independent maps and remote hidden on both sides");
    a->c->setKeys(a->c,1<<4); frames(a,b,24); a->c->setKeys(a->c,0); frames(a,b,240);
    assert(a->c->busRead8(a->c,m+18)==b->c->busRead8(b->c,m+18));
    assert(a->c->busRead8(a->c,m+13)<16 && b->c->busRead8(b->c,m+13)<16);
    puts("PASS: returning to same map respawns visitor");
    runCode(b,"FRMP_STOP()"); frames(a,b,360);
    assert(a->c->busRead8(a->c,m+12)==0 && a->c->busRead8(a->c,m+13)==16);
    assert(b->c->busRead8(b->c,m+12)==0 && b->c->busRead8(b->c,m+13)==16);
    puts("PASS: disconnect clears presence on both sides");
    assert(mScriptContextLoadFile(&b->ctx,argv[3]));
    struct mScriptEngineContext *engine=HashTableLookup(&b->ctx.engines,"lua");
    assert(engine->run(engine)); frames(a,b,180);
    assert(a->c->busRead8(a->c,m+12)==1 && b->c->busRead8(b->c,m+12)==1);
    assert(a->c->busRead8(a->c,m+13)<16 && b->c->busRead8(b->c,m+13)<16);
    puts("PASS: client reconnects and both visitors return");
    runCode(b,"FRMP_STOP()"); frames(a,b,60);
    runCode(b,"BADSOCK=socket:tcp(); assert(BADSOCK:connect('127.0.0.1',4096)); BADSOCK:send('HELLO 3 '..string.rep('0',64)..'\\n')");
    frames(a,b,60);
    assert(a->c->busRead8(a->c,m+12)==0 && a->c->busRead8(a->c,m+13)==16);
    runCode(b,"BADSOCK:close(); BADSOCK=nil");
    puts("PASS: different ROM build is rejected before applying player state");
cleanup:
    mScriptContextDetachCore(&a->ctx); mScriptContextDeinit(&a->ctx);
    mScriptContextDetachCore(&b->ctx); mScriptContextDeinit(&b->ctx);
    a->c->deinit(a->c); b->c->deinit(b->c); free(a); free(b);
    return 0;
}
