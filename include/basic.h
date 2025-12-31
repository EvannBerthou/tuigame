#ifndef BASIC_H
#define BASIC_H

#include <stddef.h>

#define TOKENS    \
    X(SEMICOLON)  \
    X(IDENTIFIER) \
    X(KEYWORD)    \
    X(STRING)     \
    X(NUMBER)     \
    X(PLUS)       \
    X(MINUS)      \
    X(STAR)       \
    X(SLASH)      \
    X(LPAREN)     \
    X(RPAREN)     \
    X(EQUAL)      \
    X(EQEQ)       \
    X(NEQ)        \
    X(LT)         \
    X(LTE)        \
    X(GT)         \
    X(GTE)        \
    X(AND)        \
    X(OR)         \
    X(NOT)        \
    X(DOT)        \
    X(UNEXPECTED) \
    X(EOF)

#define KEYWORDS \
    X(IF)        \
    X(ELSE)      \
    X(FOR)       \
    X(IN)        \
    X(WHILE)     \
    X(FUNCTION)  \
    X(END)       \
    X(TRUE)      \
    X(FALSE)

#define X(x) TOKEN_##x,
typedef enum { TOKENS } token_type;
#undef X

#define X(x) KW_##x,
typedef enum { KW_NONE, KEYWORDS } keyword_type;
#undef X

typedef struct {
    token_type type;
    keyword_type keyword;
    const char *start, *end;
} token;

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
void destroy_interpreter();

typedef enum { VAL_NUM, VAL_STRING } value_type;

typedef struct {
    value_type type;
    union {
        int number;
        const char *string;
    } as;
} value;

typedef enum { EXPR_STRING, EXPR_NUMBER, EXPR_VAR, EXPR_BINARY, EXPR_UNARY } expr_type;

typedef struct expr expr;

typedef struct {
    token_type op;
    expr *expr;
} expr_unary;

typedef struct {
    token_type op;
    expr *left;
    expr *right;
} expr_binary;

struct expr {
    expr_type type;
    union {
        int number;
        const char *string;
        const char *variable;
        expr_unary *unary;
        expr_binary *binary;
    } as;
};

struct stmt_funcall {
    const char *function;
    expr *items;
    int count;
    int capacity;
};

value eval_expr(expr *e);

#endif
