#ifndef BASIC_INTERNALS_H
#define BASIC_INTERNALS_H

#include <stddef.h>
#include <stdint.h>

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
    X(FUNC)      \
    X(RETURN)    \
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

typedef struct {
    const char *name;
    struct {
        const char **items;
        size_t count;
        size_t capacity;
    } args;
    struct {
        uint8_t *items;
        size_t count;
        size_t capacity;
    } body;
} function_code;

typedef enum {
    SYMBOL_NONE,
    SYMBOL_FUNCTION,
    SYMBOL_FUNCTION_NATIVE,
    SYMBOL_VARIABLE_INT,
    SYMBOL_VARIABLE_STRING,
} symbol_type;

typedef enum {
    SCOPE_GLOBAL,
    SCOPE_LOCAL,
} symbol_scope;

typedef struct {
    const char *name;
    symbol_type type;
    size_t depth;
    union {
        struct {
            void (*function)();
            bool variadic_arg_count;
            size_t arg_count;
        } native_func;
        int integer;
        const char *string;
        struct {
            function_code *body;
            const char **args;
            size_t arg_count;
        } funcdecl;
    } as;
} symbol;

typedef enum {
    STATE_RUNNING,
    STATE_FINISHED,
    STATE_SLEEPING,
} interpreter_state;

#define MAX_SYMBOL_COUNT 2048

typedef enum { VAL_NUM, VAL_STRING } value_type;

typedef struct {
    value_type type;
    union {
        int16_t number;
        const char *string;
    } as;
} value;

typedef enum {
    OPCODE_VARIABLE,
    OPCODE_CONSTANT_STRING,
    OPCODE_CONSTANT_NUMBER,
    OPCODE_EQEQ,
    OPCODE_NEQ,
    OPCODE_LT,
    OPCODE_LTE,
    OPCODE_GT,
    OPCODE_GTE,
    OPCODE_ADD,
    OPCODE_SUB,
    OPCODE_MULT,
    OPCODE_DIV,
    OPCODE_NEGATE,
    OPCODE_ASSIGN,
    OPCODE_FUNCALL,
    OPCODE_JUMP_IF_FALSE,
    OPCODE_JUMP,
    OPCODE_RETURN,
    OPCODE_DISCARD,
    OPCODE_EOF,
} opcode_type;

typedef struct {
    function_code *function;
    size_t ip;
    size_t sp;
    size_t symbols_count;
} return_frame;

struct basic_interpreter {
    void (*print_fn)(const char *text);
    void (*append_print_fn)(const char *text);

    symbol symbols_table[MAX_SYMBOL_COUNT];
    size_t symbols_table_count;
    size_t depth;

    interpreter_state state;
    float time_elapsed;
    float wakeup_time;

    struct {
        function_code *items;
        size_t count;
        size_t capacity;
    } bytecode;

    function_code *current_function;
    size_t ip;
    size_t sp;

    struct {
        value *items;
        size_t count;
        size_t capacity;
    } values;

    struct {
        value *items;
        size_t count;
        size_t capacity;
    } stack;

    struct {
        return_frame *items;
        size_t count;
        size_t capacity;
    } return_stack;
};

symbol *get_symbol_id(size_t idx);
size_t create_symbol(const char *name, symbol_type type);

#endif
