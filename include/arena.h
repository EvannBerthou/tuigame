#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

#define DYNARRAY_CAPACITY_LIMIT 65536

// TODO: Could use arena
#define arena_append(l, x)                                                                                  \
    do {                                                                                                    \
        if ((l)->count == (l)->capacity) {                                                                  \
            (l)->capacity = (l)->capacity == 0 ? 64 : (l)->capacity * 2;                                    \
            (l)->items = arena_realloc(interpreter_arena, (l)->items, sizeof(*(l)->items) * (l)->capacity); \
        }                                                                                                   \
        (l)->items[(l)->count++] = (x);                                                                     \
    } while (0)

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
    size_t block_size;
    size_t padding;
} arena_block_header;

typedef struct arena_block {
    size_t block_size;
    struct arena_block *next;
} arena_block;

typedef struct {
    char *content;
    size_t size;
    size_t used;

    arena_block *head;
} arena;

#define ARENA_DEFAULT_SIZE (4 * 1024 * 1024)

arena *arena_default();
void *arena_alloc(arena *a, size_t size);
void *arena_realloc(arena *a, void *ptr, size_t size);
void arena_free_node(arena *a, void *ptr);
void arena_free(arena *a);

#endif
