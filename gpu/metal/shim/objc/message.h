#pragma once
#include <objc/objc.h>
#ifdef __cplusplus
extern "C" {
#endif
id   objc_msgSend(id self, SEL op, ...);
void objc_msgSend_stret(void* stret, id self, SEL op, ...);
double objc_msgSend_fpret(id self, SEL op, ...);
#ifdef __cplusplus
}
#endif
