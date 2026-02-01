#include "arena.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

arena *arena_default() {
    arena *a = malloc(sizeof(*a));
    assert(a);
    a->size = ARENA_DEFAULT_SIZE;
    a->content = malloc(a->size);
    a->head = (arena_block *)a->content;
    a->head->block_size = a->size;
    a->head->next = NULL;
    a->used = 0;
    assert(a->content);
    return a;
}

static void arena_remove_node(arena_block **head, arena_block *prev, arena_block *del) {
    if (prev == NULL) {
        *head = del->next;
    } else {
        prev->next = del->next;
    }
}

static size_t align_up(size_t n, size_t align) {
    return (n + align - 1) & ~(align - 1);
}

static inline int are_adjacent(arena_block *a, arena_block *b) {
    return (char *)a + a->block_size == (char *)b;
}

static void arena_insert_sorted(arena_block **head, arena_block *block) {
    arena_block *node = *head;
    arena_block *prev = NULL;

    while (node && node < block) {
        prev = node;
        node = node->next;
    }

    block->next = node;

    if (prev) {
        prev->next = block;
    } else {
        *head = block;
    }
}

static void arena_coalesce(arena_block **head, arena_block *block) {
    arena_block *node = *head;
    arena_block *prev = NULL;

    while (node && node != block) {
        prev = node;
        node = node->next;
    }

    if (block->next && are_adjacent(block, block->next)) {
        block->block_size += block->next->block_size;
        block->next = block->next->next;
    }

    if (prev && are_adjacent(prev, block)) {
        prev->block_size += block->block_size;
        prev->next = block->next;
    }
}

static arena_block *arena_next_block(arena *a, arena_block *block) {
    char *end = (char *)block + block->block_size;
    char *arena_end = (char *)a->content + a->size;

    if (end >= arena_end)
        return NULL;

    return (arena_block *)end;
}

void *arena_alloc(arena *a, size_t size) {
    const size_t alignment = alignof(max_align_t);

    size = align_up(size, alignment);
    size_t required = size + sizeof(arena_block_header);

    arena_block *node = a->head;
    arena_block *prev_node = NULL;

    while (node != NULL) {
        if (node->block_size >= required) {
            break;
        }
        prev_node = node;
        node = node->next;
    }

    if (node == NULL) {
        assert(0 && "Failed to allocate memory.");
        return NULL;
    }

    size_t remanining = node->block_size - required;
    arena_remove_node(&a->head, prev_node, node);

    if (remanining >= sizeof(arena_block)) {
        arena_block *new_node = (arena_block *)((char *)node + required);
        new_node->block_size = remanining;
        arena_insert_sorted(&a->head, new_node);
    }

    arena_block_header *header = (arena_block_header *)node;
    header->block_size = required;
    header->padding = 0;

    a->used += required;

    return (void *)((char *)header + sizeof(arena_block_header));
}

void *arena_realloc(arena *a, void *ptr, size_t size) {
    if (ptr == NULL) {
        return arena_alloc(a, size);
    }

    if (size == 0) {
        arena_free_node(a, ptr);
        return NULL;
    }

    arena_block_header *header = (arena_block_header *)((char *)ptr - sizeof(arena_block_header));
    size_t old_total = header->block_size;
    size_t old_user_size = old_total - sizeof(arena_block_header);

    size = align_up(size, alignof(max_align_t)) + sizeof(arena_block_header);

    if (size <= old_total) {
        return ptr;
    }

    if (a->used + size >= a->size) {
        assert(0 && "No more free memory");
        return NULL;
    }

    arena_block *block = (arena_block *)header;
    arena_block *next = arena_next_block(a, block);

    if (next) {
        arena_block *node = a->head;
        arena_block *prev = NULL;
        while (node && node != next) {
            prev = node;
            node = node->next;
        }

        if (node) {
            size_t combined = block->block_size + node->block_size;
            // Next block is free and we can merge them
            if (combined >= size) {
                arena_remove_node(&a->head, prev, node);
                size_t remanining = combined - size;
                if (remanining >= sizeof(arena_block)) {
                    arena_block *split = (arena_block *)((char *)block + size);
                    split->block_size = remanining;
                    arena_insert_sorted(&a->head, split);
                }

                header->block_size = size;
                a->used += (size - old_total);
                memset((char *)ptr + old_user_size, 0xAA, size - old_user_size);
                return ptr;
            }
        }
    }

    void *new_ptr = arena_alloc(a, size);
    if (new_ptr == NULL) {
        return NULL;
    }

    memcpy(new_ptr, ptr, old_user_size);
    arena_free_node(a, ptr);
    return new_ptr;
}

void arena_free_node(arena *a, void *ptr) {
    if (ptr == NULL) {
        return;
    }

    arena_block_header *header = (arena_block_header *)((char *)ptr - sizeof(arena_block_header));
    arena_block *block = (arena_block *)header;

    size_t block_size = block->block_size;
    block->block_size = header->block_size;
    a->used -= block_size;

    arena_insert_sorted(&a->head, block);
    arena_coalesce(&a->head, block);
}

void arena_free(arena *a) {
    if (a) {
        free(a->content);
    }
    free(a);
}
