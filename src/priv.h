#ifndef CAPSULE_PRIV_H
#define CAPSULE_PRIV_H

#include "capsule.h"

void gc_mark(Capsule root);

void gc();

char* slurp(const char* path);

void load_file(Capsule env, const char* path);

void define_builtin(Capsule scope);

#endif