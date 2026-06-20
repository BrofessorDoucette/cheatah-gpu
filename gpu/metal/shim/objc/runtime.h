#pragma once
#include <objc/objc.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct objc_protocol* Protocol;
Class  objc_lookUpClass(const char* name);
Class  objc_getClass(const char* name);
Protocol* objc_getProtocol(const char* name);
SEL    sel_registerName(const char* name);
const char* sel_getName(SEL sel);
Class  object_getClass(id obj);
const char* class_getName(Class cls);
id     objc_retain(id obj);
void   objc_release(id obj);
id     objc_autorelease(id obj);
#ifdef __cplusplus
}
#endif
