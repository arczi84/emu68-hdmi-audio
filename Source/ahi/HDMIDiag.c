/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <exec/execbase.h>
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
#include "diag.h"
static ULONG be32(const unsigned char *p)
{
    return ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3];
}
/* AHI preferences are big-endian IFF. Read files only: opening ahi.device
 * or playing a test here could replace the state we need to diagnose. */
static void show_prefs(const char *path)
{
    unsigned char data[4096];
    FILE *f=fopen(path,"rb");
    size_t size,limit,pos;
    int extra,found=0;
    if(!f) { printf("%s: unavailable\n",path); return; }
    size=fread(data,1,sizeof(data),f);
    extra=fgetc(f);
    if(ferror(f)) { fclose(f); printf("%s: read error\n",path); return; }
    fclose(f);
    printf("%s (saved settings, not live unit state):\n",path);
    if(extra!=EOF || size<12 || memcmp(data,"FORM",4) ||
       memcmp(data+8,"PREF",4) || be32(data+4)<4 || be32(data+4)>size-8) {
        puts("  Unsupported, oversized or incomplete preferences"); return;
    }
    limit=be32(data+4)+8;
    pos=12;
    while(pos<limit) {
        ULONG length;
        const unsigned char *p;
        if(limit-pos<8) { puts("  Truncated chunk header"); return; }
        length=be32(data+pos+4);
        if(length>limit-pos-8) { puts("  Truncated chunk"); return; }
        p=data+pos+8;
        if(!memcmp(data+pos,"AHIU",4)) {
            if(length<12) { puts("  Short AHIU chunk"); return; }
            if(p[0]<=3 || p[0]==255) {
                ULONG mode=be32(p+4);
                printf("  Unit %u%s: mode=%08x%s Hz=%u channels=%u\n",
                    (unsigned)p[0],p[0]==255?" (Music)":"",mode,
                    mode==0x001f0010UL?" HDMI":mode==0x001f0011UL?" HDMI HiFi":"",be32(p+8),
                    ((unsigned)p[2]<<8)|p[3]);
                if(p[0]==0) found=1;
            }
        }
        pos+=8+length;
        if(length&1) {
            if(pos==limit) { puts("  Missing chunk padding"); return; }
            ++pos;
        }
    }
    if(!found) puts("  Unit 0 missing: AHI must choose its default mode");
}
int main(void)
{
    struct Library *driver=NULL;
    Forbid();
    if(FindName(&SysBase->LibList,(CONST_STRPTR)"emu68hdmi.audio"))
        driver=OpenLibrary((CONST_STRPTR)"emu68hdmi.audio",4);
    Permit();
    puts("HDMIDiag 0.4 - read-only; run during game silence before another audio test");
    if(!driver) puts("HDMI driver not resident; do not start a tone test before recording this result.");
    else {
        printf("Resident driver %u.%u\n",driver->lib_Version,driver->lib_Revision);
        hdmi_diag(driver);
        CloseLibrary(driver);
    }
    show_prefs("ENV:Sys/ahi.prefs");
    show_prefs("ENVARC:Sys/ahi.prefs");
    return driver?0:20;
}
