#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#define DYNARRAY_CAPACITY_LIMIT 65536

// TODO: Could use arena
#define append(l, x)                                                               \
    do {                                                                           \
        if ((l)->count == (l)->capacity) {                                         \
            (l)->capacity = (l)->capacity == 0 ? 64 : (l)->capacity * 2;           \
            (l)->items = realloc((l)->items, sizeof(*(l)->items) * (l)->capacity); \
        }                                                                          \
        (l)->items[(l)->count++] = (x);                                            \
    } while (0)

#define pop(l) (l)->items[--(l)->count]

typedef struct {
    char *content;
    size_t ptr;
    size_t size;
} arena;

#define ARENA_DEFAULT_SIZE (2 * 1024 * 1024)

arena *arena_default();
void *arena_alloc(arena *a, size_t size);
void *arena_realloc(arena *a, void *ptr, size_t size);
void arena_free(arena *a);

#endif
