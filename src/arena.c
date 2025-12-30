#include "arena.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

arena *arena_default() {
    arena *a = malloc(sizeof(*a));
    assert(a);
    a->size = ARENA_DEFAULT_SIZE;
    a->content = malloc(a->size);
    assert(a->content);
    a->ptr = 0;
    return a;
}

void *arena_alloc(arena *a, size_t size) {
    assert(a->ptr + size < a->size && "Too much allocation for arena");
    void *result = a->content + a->ptr;
    a->ptr += size;
    return result;
}

void arena_free(arena *a) {
    free(a->content);
    free(a);
}
