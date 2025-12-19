#ifndef BASIC_H
#define BASIC_H

#include <stddef.h>

typedef struct stmt_funcall stmt_funcall;

typedef struct {
    const char *name;
    enum {
        SYMBOL_NONE,
        SYMBOL_FUNCTION,
        SYMBOL_VARIABLE_INT,
        SYMBOL_VARIABLE_STRING,
    } type;
    union {
        void (*function)(stmt_funcall *);
        int integer;
        const char *string;
    } as;
} symbol;

typedef struct {
    void (*print_fn)(const char *text);
    void (*append_print_fn)(const char *text);
    symbol symbols_table[128];
    size_t symbol_count;
} basic_interpreter;

void register_function(basic_interpreter *i, const char *name, void (*f)(stmt_funcall *));
void execute_program(basic_interpreter *i, const char *src);

#endif
