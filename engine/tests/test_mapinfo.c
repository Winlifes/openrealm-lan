#include "test.h"
#include "common/shared.h"
#include "common/mapinfo_read.h"
#include <stdlib.h>

typedef struct mapAllocation_s {struct mapAllocation_s *next;void *data;} mapAllocation_t;
static mapAllocation_t *map_allocations;
static handle_t map_alloc(long size){mapAllocation_t *a=malloc(sizeof(*a));a->data=calloc(1,size);a->next=map_allocations;map_allocations=a;return a->data;}
static void map_free(handle_t p){(void)p;}
static void map_clear(void){while(map_allocations){mapAllocation_t *a=map_allocations;map_allocations=a->next;free(a->data);free(a);}}
static void put_bytes(uint8_t **p,void const *data,uint32_t n){memcpy(*p,data,n);*p+=n;}
static void put_word(uint8_t **p,uint32_t value){put_bytes(p,&value,4);}
static void put_string(uint8_t **p,char const *s){put_bytes(p,s,strlen(s)+1);}
static void put_zeros(uint8_t **p,uint32_t n){memset(*p,0,n);*p+=n;}

static uint32_t make_info(uint8_t *data,uint32_t version,uint32_t editor){
    uint8_t *p=data;put_word(&p,version);put_word(&p,1);put_word(&p,editor);
    if(version>=28){put_word(&p,3);put_word(&p,0);put_word(&p,1);put_word(&p,24371);}
    put_string(&p,"Map");put_string(&p,"Author");put_string(&p,"Description");put_string(&p,"2 players");
    put_zeros(&p,sizeof(mapCameraBounds_t));put_word(&p,64);put_word(&p,64);put_word(&p,0);
    *p++='L';put_word(&p,0);if(version>=39)put_word(&p,4);
    put_string(&p,"Loading.mdl");put_string(&p,"Text");put_string(&p,"Title");put_string(&p,"Subtitle");
    put_word(&p,0);FOR_LOOP(i,4)put_string(&p,"");
    put_zeros(&p,20);if(version>=39)put_zeros(&p,24);
    put_word(&p,0);put_string(&p,"");*p++='L';put_word(&p,0xffffffff);
    if(version>=28)put_word(&p,0);
    if(version>=31){put_word(&p,1);put_word(&p,3);}
    if(version>=32){put_word(&p,1650);put_word(&p,3000);}
    if(version>=33)put_word(&p,1000);
    if(version>=39)FOR_LOOP(i,10)put_word(&p,11+i);
    put_word(&p,2);
    FOR_LOOP(i,2){
        put_word(&p,i);put_word(&p,i?2:1);put_word(&p,1);
        if(version>=39&&editor>=6128)put_word(&p,0x12345678);
        put_word(&p,0x80+i);put_string(&p,i?"Second":"First");put_zeros(&p,8);
        put_word(&p,1);put_word(&p,2);if(version>=31){put_word(&p,4);put_word(&p,8);}
    }
    put_word(&p,1);put_word(&p,0);put_word(&p,3);put_string(&p,"Team");
    return p-data;
}

TEST(mapinfo, legacy_and_modern_editor_layouts_share_one_decoder){
    uint32_t versions[]={25,39,39},editors[]={6059,6127,6128};uint8_t bytes[1024];
    FOR_LOOP(i,3){
        uint32_t size=make_info(bytes,versions[i],editors[i]),consumed=0;mapInfo_t info={0};
        T_ASSERT(MapInfo_ParseSetup(bytes,size,&info,map_alloc,map_free,&consumed));
        T_EQ(consumed,size);T_STREQ(info.mapName,"Map");T_STREQ(info.loadingScreenModel,"Loading.mdl");
        T_EQ(info.players[0].flags,0x80);T_EQ(info.players[1].flags,0x81);
        T_STREQ(info.players[1].playerName,"Second");T_EQ(info.num_teams,1);T_EQ(info.teams[0].playerMasks,3);
        if(versions[i]>=39){T_EQ(info.loadingScreenRace,4);T_EQ(info.waterOverrides[9],20);}
        map_clear();
    }
}

TEST(mapinfo, every_truncated_prefix_fails_without_reading_past_input){
    uint8_t bytes[1024];uint32_t size=make_info(bytes,39,6128);
    FOR_LOOP(cut,size){mapInfo_t info={0};T_ASSERT(!MapInfo_ParseSetup(bytes,cut,&info,map_alloc,map_free,NULL));map_clear();}
}
