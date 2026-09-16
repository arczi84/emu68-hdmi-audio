/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#define CAPACITY 4096
struct prefs_file {
    const char *path;
    char backup[100];
    unsigned char data[CAPACITY],original[CAPACITY];
    size_t size,offset;
    int changed,dirty;
};
static struct prefs_file files[2] = {
    { .path="ENV:Sys/ahi.prefs" }, { .path="ENVARC:Sys/ahi.prefs" }
};
static ULONG be32(const unsigned char *p)
{
    return ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3];
}
static int absent(const char *path)
{
    BPTR lock=Lock((CONST_STRPTR)path,ACCESS_READ);
    if(lock) { UnLock(lock); return 0; }
    return IoErr()==ERROR_OBJECT_NOT_FOUND;
}
static void failure(const char *operation,const char *path,LONG error)
{
    printf("FAILED: %s %s (DOS error %ld)\n",operation,path,(long)error);
    fflush(stdout);
    if(error) PrintFault(error,(CONST_STRPTR)operation);
}
static int prepare(struct prefs_file *p)
{
    FILE *f=fopen(p->path,"rb");
    size_t pos,offset=0;
    int extra,failed,count=0,i;
    if(!f) { failure("read preferences",p->path,IoErr()); return 0; }
    p->size=fread(p->data,1,CAPACITY,f);
    extra=fgetc(f); failed=ferror(f); fclose(f);
    if(failed || extra!=EOF || p->size<12 || memcmp(p->data,"FORM",4) ||
       memcmp(p->data+8,"PREF",4) || be32(p->data+4)!=p->size-8) return 0;
    for(pos=12;pos<p->size;) {
        ULONG n;
        if(p->size-pos<8) return 0;
        n=be32(p->data+pos+4);
        if(n>p->size-pos-8) return 0;
        if(!memcmp(p->data+pos,"AHIU",4)) {
            if(n<12) return 0;
            if(p->data[pos+8]==0) { offset=pos+12; ++count; }
        }
        pos+=8+n;
        if(n&1) { if(pos==p->size) return 0; ++pos; }
    }
    if(count!=1) return 0;
    memcpy(p->original,p->data,p->size);
    p->offset=offset;
    printf("%s: Unit 0 mode %08x -> 001f0010 (HDMI)\n",p->path,be32(p->data+offset));
    if(be32(p->data+offset)==0x001f0010UL) return 1;
    /* Change only the four-byte mode ID. Keep frequency, channels, volumes,
     * other units, unknown chunks and padding byte-for-byte. */
    p->data[offset]=0; p->data[offset+1]=0x1f;
    p->data[offset+2]=0; p->data[offset+3]=0x10;
    p->changed=1;
    for(i=0;i<100;++i) {
        snprintf(p->backup,sizeof(p->backup),"%s.before-hdmi0-%d",p->path,i);
        if(absent(p->backup)) break;
    }
    if(i==100) return 0;
    return 1;
}
static int matches(const char *path,const unsigned char *expected,size_t size)
{
    FILE *f;
    unsigned char data[CAPACITY];
    size_t got;
    int extra,ok;
    f=fopen(path,"rb");
    if(!f) return 0;
    got=fread(data,1,sizeof(data),f); extra=fgetc(f);
    ok=!ferror(f) && extra==EOF && got==size && !memcmp(data,expected,size);
    fclose(f);
    return ok;
}
static int backup(struct prefs_file *p)
{
    BPTR f;
    LONG wrote,error,closed;
    int i;
    if(!p->changed) return 1;
    /* Recheck names now: ENV/ENVARC can resolve to overlapping directories. */
    for(i=0;i<100;++i) {
        snprintf(p->backup,sizeof(p->backup),"%s.before-hdmi0-%d",p->path,i);
        if(absent(p->backup)) break;
    }
    if(i==100) { failure("choose backup",p->path,0); return 0; }
    f=Open((CONST_STRPTR)p->backup,MODE_NEWFILE);
    if(!f) { failure("open backup",p->backup,IoErr()); return 0; }
    wrote=Write(f,p->original,p->size); error=IoErr();
    closed=Close(f);
    if(wrote!=(LONG)p->size || !closed) {
        failure("write backup",p->backup,wrote!=(LONG)p->size?error:IoErr()); return 0;
    }
    if(!matches(p->backup,p->original,p->size)) {
        failure("verify backup",p->backup,0); return 0;
    }
    printf("Backup verified: %s\n",p->backup);
    return 1;
}
static int patch(struct prefs_file *p,int restore)
{
    const unsigned char *expected=restore?p->original:p->data;
    BPTR f;
    LONG wrote,error,closed;
    if(matches(p->path,expected,p->size)) return 1;
    if(!restore && !matches(p->path,p->original,p->size)) {
        failure("preferences changed since read",p->path,0); return 0;
    }
    /* No renaming, deleting or truncating the preference file. Only its
     * existing Unit 0 ID is written, after both original backups verify. */
    f=Open((CONST_STRPTR)p->path,MODE_OLDFILE);
    if(!f) { failure("open preferences",p->path,IoErr()); return 0; }
    if(Seek(f,p->offset,OFFSET_BEGINNING)==-1) {
        error=IoErr(); Close(f); failure("seek Unit 0",p->path,error); return 0;
    }
    p->dirty=1;
    wrote=Write(f,(APTR)(expected+p->offset),4); error=IoErr();
    closed=Close(f);
    if(wrote!=4 || !closed) {
        failure(restore?"restore Unit 0":"write Unit 0",p->path,wrote!=4?error:IoErr()); return 0;
    }
    if(!matches(p->path,expected,p->size)) {
        failure(restore?"verify restore":"verify Unit 0",p->path,0); return 0;
    }
    return 1;
}
int main(int argc,char **argv)
{
    int i,j;
    (void)argv;
    puts("SetHDMIUnit0 0.2 - write Unit 0 ID without renaming preference files");
    if(argc!=1) { puts("Usage: SetHDMIUnit0"); return 20; }
    for(i=0;i<2;++i) if(!prepare(&files[i])) {
        printf("Cannot safely read/prepare %s. Preferences unchanged.\n",files[i].path);
        return 20;
    }
    for(i=0;i<2;++i) if(!backup(&files[i])) {
        puts("Cannot create verified backups. Preferences unchanged."); return 20;
    }
    for(i=0;i<2;++i) {
        struct prefs_file *p=&files[i];
        if(!p->changed) continue;
        if(!patch(p,0)) break;
    }
    if(i!=2) {
        puts("Installation failed; restoring originals.");
        for(j=0;j<2;++j) if(files[j].dirty) {
            if(!patch(&files[j],1))
                printf("RESTORE REQUIRED: copy %s to %s\n",files[j].backup,files[j].path);
        }
        return 20;
    }
    puts("Unit 0 saved as HDMI. Restart Amiga before testing games.");
    puts("AHI Prefs still hides this mode in 4.11; do not resave Unit 0 there.");
    return 0;
}
