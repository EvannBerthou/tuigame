#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct {
    char *content;
    size_t ptr;
    size_t size;
} arena;

#define ARENA_DEFAULT_SIZE (8 * 1024)

arena *arena_default();
void *arena_alloc(arena *a, size_t size);
void arena_free(arena *a);

#endif
