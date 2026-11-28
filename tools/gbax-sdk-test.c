/* SPDX-License-Identifier: MPL-2.0 */
#include "gbax.h"
#define CHECK(condition,code) do {if(!(condition))return 0xbad10000u+(code);}while(0)
uint32_t sdk_test(void) {
    GBAXInfo info;
    if(!GBAXDetect(&info))return 0x600d0000u;
    CHECK(GBAXHas(&info,GBAX_CAP_RAM64|GBAX_CAP_DMA3|GBAX_CAP_TURBO),1);
    CHECK(info.workSize==0x03000000u && info.multiplier==1,2);
    GBAXArena root,maps,ai;
    CHECK(GBAXArenaInit(&root,&info),3);
    CHECK(GBAXReserve(&root,&maps,0x1000000u,65536),4);
    CHECK(GBAXReserve(&root,&ai,0x800000u,65536),5);
    CHECK(maps.end<=ai.begin && GBAXRemaining(&root)==0x1800000u,6);
    CHECK(!GBAXAlloc(&root,0x1800001u,4) && !GBAXAlloc(&maps,4,3),7);
    volatile uint32_t* first=GBAXAlloc(&maps,16,4);
    CHECK(first!=NULL,8);
    uint32_t checkpoint=GBAXCheckpoint(&maps);
    volatile uint32_t* second=GBAXAlloc(&maps,16,16);
    CHECK(second!=NULL && first!=second,9);
    first[0]=0x11223344u;first[1]=0xaabbccddu;
    CHECK(GBAXCopy32(&info,(void*)second,(const void*)first,2),10);
    CHECK(second[0]==first[0] && second[1]==first[1],11);
    CHECK(!GBAXCopy32(&info,(void*)first,(const void*)first,2),12);
    CHECK(GBAXRewind(&maps,checkpoint) && GBAXAlloc(&maps,16,16)==(void*)second,13);
    CHECK(!GBAXRewind(&maps,ai.begin),14);
    CHECK(GBAXHas(&info,GBAX_CAP_ROM128) && info.romSize==0x08000000u,15);
    const volatile uint32_t* asset=GBAXAsset(&info,0x07fffffcu,4);
    CHECK(asset && *asset==0x12345678u,16);
    CHECK(!GBAXAsset(&info,0x07fffffcu,5),17);
    CHECK(GBAXCopy32(&info,(void*)second,(const void*)asset,1) && second[0]==*asset,18);
    CHECK(GBAXHas(&info,GBAX_CAP_SAVX),19);
    CHECK(!GBAXSaveBuffer(&info,0,4),20);
    uint32_t status=GBAXLoadSave(&info);
    CHECK(status==GBAX_SDK_EMPTY || status==GBAX_SDK_LOADED,21);
    volatile uint32_t* save=GBAXSaveBuffer(&info,0x00fffffcu,4);
    CHECK(save && !GBAXSaveBuffer(&info,0x00fffffcu,5),22);
    if(status==GBAX_SDK_EMPTY) {
        *save=0xfeedcafeu;
        CHECK(GBAXCommitSave(&info)==GBAX_SDK_COMMITTED,23);
        return 0x600d0001u;
    }
    CHECK(*save==0xfeedcafeu,24);
    return 0x600d0002u;
}
