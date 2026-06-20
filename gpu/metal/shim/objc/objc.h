#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct objc_object* id;
typedef struct objc_class* Class;
typedef struct objc_selector* SEL;
typedef struct objc_object* (*IMP)(id, SEL, ...);
typedef signed char BOOL;
#define YES ((BOOL)1)
#define NO  ((BOOL)0)
#ifndef nil
#define nil ((id)0)
#endif
#ifndef Nil
#define Nil ((Class)0)
#endif
struct objc_object { Class isa; };
