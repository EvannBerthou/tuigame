#ifndef BASIC_H
#define BASIC_H

#include <stddef.h>

typedef struct stmt_funcall stmt_funcall;

typedef enum {
    SYMBOL_NONE,
    SYMBOL_FUNCTION,
    SYMBOL_VARIABLE_INT,
    SYMBOL_VARIABLE_STRING,
} symbol_type;

typedef struct {
    const char *name;
    symbol_type type;
    union {
        void (*function)(stmt_funcall *);
        int integer;
        const char *string;
    } as;
} symbol;

typedef enum {
    STATE_RUNNING,
    STATE_FINISHED,
    STATE_SLEEPING,
} interpreter_state;

typedef struct stmt stmt;

#define MAX_SYMBOL_COUNT 2048

typedef struct {
    void (*print_fn)(const char *text);
    void (*append_print_fn)(const char *text);
    symbol symbols_table[MAX_SYMBOL_COUNT];
    size_t symbol_count;

    interpreter_state state;
    stmt *pc;

    float time_elapsed;
    float wakeup_time;
} basic_interpreter;

void init_interpreter(basic_interpreter *i, const char *src);
bool step_program(basic_interpreter *i);
void register_function(basic_interpreter *i, const char *name, void (*f)(stmt_funcall *));
void advance_interpreter_time(basic_interpreter *i, float time);

// TEMP

typedef enum { VAL_NUM, VAL_BOOL, VAL_STRING } value_type;

typedef struct {
    value_type type;
    union {
        int number;
        bool boolean;
        const char *string;
    } as;
} value;

typedef enum {
    EXPR_BOOL,
    EXPR_STRING,
    EXPR_NUMBER,
    EXPR_VAR,
} expr_type;

typedef struct {
    expr_type type;
    union {
        bool boolean;
        int number;
        const char *string;
        const char *variable;
    } as;
} expr;

struct stmt_funcall {
    const char *function;
    expr *items;
    int count;
    int capacity;
};

value eval_expr(expr *e);

#endif
