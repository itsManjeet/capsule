#ifndef CAPSULE_PRIV_H
#define CAPSULE_PRIV_H

#include "../include/capsule.h"

void define_builtins(Capsule scope);
char *readfile(const char *path);
void gc_mark(Capsule root);
void gc();
void print_error(const char *filename, const char *source, Capsule error);

#endif