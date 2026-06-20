#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef double CFTimeInterval;
typedef const void* CFTypeRef;
typedef const struct __CFString* CFStringRef;
typedef const struct __CFArray*  CFArrayRef;
typedef const struct __CFAllocator* CFAllocatorRef;
typedef long CFIndex;
extern const CFAllocatorRef kCFAllocatorDefault;
typedef struct { CFIndex version; const void* a; const void* b; const void* c; const void* d; const void* e; } CFArrayCallBacks;
extern const CFArrayCallBacks kCFTypeArrayCallBacks;
CFArrayRef CFArrayCreate(CFAllocatorRef, const void**, CFIndex, const CFArrayCallBacks*);
CFStringRef __CFStringMakeConstantString(const char*);
#define CFSTR(s) __CFStringMakeConstantString(s)
#ifdef __cplusplus
}
#endif
