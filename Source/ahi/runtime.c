/* SPDX-License-Identifier: GPL-3.0-or-later
 * No application startup or libnix task-local state in a shared library. */
#include <stddef.h>
void *memcpy(void *dst,const void *src,size_t n) {
    unsigned char *d=dst; const unsigned char *s=src;
    while(n--) *d++=*s++;
    return dst;
}
void *memset(void *dst,int value,size_t n) {
    unsigned char *d=dst; while(n--) *d++=(unsigned char)value; return dst;
}
void *memchr(const void *src,int value,size_t n) {
    const unsigned char *s=src;
    while(n--) { if(*s==(unsigned char)value) return (void *)s; ++s; } return NULL;
}
int strcmp(const char *a,const char *b) {
    while(*a && *a==*b) { ++a; ++b; } return (unsigned char)*a-(unsigned char)*b;
}
