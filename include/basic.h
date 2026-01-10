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

typedef struct stmt_funcall stmt_funcall;

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

typedef struct stmt stmt;

typedef struct {
    const char *name;
    symbol_type type;
    symbol_scope scope;
    size_t scope_depth;
    union {
        struct {
            void (*function)(stmt_funcall *);
            bool variadic_arg_count;
            size_t arg_count;
        } native_func;
        int integer;
        const char *string;
        struct {
            stmt *body;
            const char **args;
            size_t arg_count;
        } funcdecl;
    } as;
} symbol;

typedef enum {
    STATE_RUNNING,
    STATE_EXPR_EVALUATION,
    STATE_CONTINUE_EXPR_EVALUATION,  // TODO: WTF
    STATE_FINISHED,
    STATE_SLEEPING,
} interpreter_state;

typedef struct stmt stmt;
typedef struct expr expr;

#define MAX_SYMBOL_COUNT 2048

typedef struct {
    stmt *return_stmt;
    size_t stack_idx;
    bool is_expr_call;
    int expr_pc;
} return_call;

typedef struct {
    return_call *items;
    int count;
    int capacity;
} return_stack;

typedef struct {
    expr **items;
    int count;
    int capacity;
} expr_stack;

typedef enum { VAL_NUM, VAL_STRING } value_type;

typedef struct {
    value_type type;
    union {
        int number;
        const char *string;
    } as;
} value;

typedef enum { EXPR_STRING, EXPR_NUMBER, EXPR_VAR, EXPR_BINARY, EXPR_UNARY, EXPR_FUNCALL } expr_type;

typedef struct {
    token_type op;
    expr *expr;
} expr_unary;

typedef struct {
    token_type op;
    expr *left;
    expr *right;
} expr_binary;

typedef struct {
    const char *name;
    struct {
        expr *items;
        int count;
        int capacity;
    } args;
} expr_funcall;

struct expr {
    expr_type type;
    union {
        int number;
        const char *string;
        const char *variable;
        expr_unary *unary;
        expr_binary *binary;
        expr_funcall funcall;
    } as;
};

struct stmt_funcall {
    const char *function;
    expr *items;
    int count;
    int capacity;
};

typedef enum {
    OP_PUSH_NUMBER,
    OP_PUSH_STRING,
    OP_PUSH_VAR,
    OP_UNARY_OP,
    OP_BINARY_OP,
    OP_CALL_FUNC,
    OP_NOP
} expr_op_type;

typedef struct {
    expr_op_type type;
    union {
        int number;
        const char *string;
        const char *variable;
        token_type op;
        struct {
            const char *name;
            int argc;
            bool stmt;
        } func;
    } as;
} expr_op;

typedef struct {
    expr_op *items;
    int count;
    int capacity;
} expr_ops;

typedef struct {
    value *items;
    int count;
    int capacity;
} value_stack;

typedef enum {
    STMT_NOP,
    STMT_EXPR,
    STMT_FUNCALL,
    STMT_IF,
    STMT_FOR,
    STMT_FOREND,
    STMT_WHILE,
    STMT_WHILEEND,
    STMT_VARIABLE,
    STMT_FUNCDECL,
    STMT_FUNCTION_EXIT,
    STMT_RETURN,
} stmt_type;

typedef struct stmt stmt;

typedef struct {
    expr expr;
} stmt_expr;

typedef struct {
    expr condition;
} stmt_if;

typedef struct {
    const char *variable;
    expr min;
    expr max;
} stmt_for;

typedef struct {
    const char *variable;
    int min;
    int max;
    size_t stack_saved;
} stmt_for_end;

typedef struct {
    expr condition;
} stmt_while;

typedef struct {
    const char *variable;
    expr expr;
} stmt_variable;

typedef struct {
    const char *name;
    struct {
        const char **items;
        int count;
        int capacity;
    } args;
    stmt *body;
} stmt_funcdecl;

typedef struct {
    expr expr;
} stmt_return;

struct stmt {
    stmt_type type;
    union {
        stmt_expr stmt_expr;
        stmt_funcall stmt_funcall;
        stmt_if stmt_if;
        stmt_for stmt_for;
        stmt_for_end stmt_for_end;
        stmt_while stmt_while;
        stmt_variable stmt_variable;
        stmt_funcdecl stmt_funcdecl;
        stmt_return stmt_return;
    } as;

    stmt *next;
    stmt *jmp;
};

typedef enum {
    CONT_NONE,
    CONT_ASSIGN,
    CONT_IF,
    CONT_DISCARD,
    CONT_LOOP_MIN,
    CONT_LOOP_MAX,
    CONT_WHILE,
} continuation_type;

typedef struct {
    continuation_type type;
    union {
        struct {
            const char *variable;
            stmt *next;
        } stmt_assign;
        struct {
            stmt *if_branch;
            stmt *else_branch;
        } stmt_if;
        struct {
            stmt *next;
        } stmt_discard;
        struct {
            stmt_for *entry;
            stmt *end;
        } cont_stmt_for;
        struct {
            stmt *entry;
            stmt *out;
        } cont_stmt_while;
    } as;
} continuation;

typedef struct {
    expr_ops plan;
    size_t plan_idx;
    size_t value_stack_idx;
    continuation continuation;
    bool own_returns;
    bool pending_return;
} expr_frame;

typedef struct {
    expr_frame *items;
    int count;
    int capacity;
} expr_frame_stack;

typedef struct {
    void (*print_fn)(const char *text);
    void (*append_print_fn)(const char *text);
    symbol symbols_table[MAX_SYMBOL_COUNT];
    size_t symbol_count;

    interpreter_state state;
    stmt *pc;
    return_stack returns;
    size_t scope_depth;

    expr_frame_stack expr_frames;
    expr_ops current_expr_plan;
    size_t current_expr_plan_idx;
    value_stack values_stack;

    float time_elapsed;
    float wakeup_time;

    bool pending_return;
} basic_interpreter;

value eval_expr(expr *e);
void init_interpreter(basic_interpreter *i, const char *src);
bool step_program(basic_interpreter *i);
void register_function(const char *name, void (*f)(stmt_funcall *), int arg_count);
void register_variable_int(const char *name, int value);
void register_variable_string(const char *name, const char *value);
void advance_interpreter_time(basic_interpreter *i, float time);
void destroy_interpreter();

void basic_push_function_result(int result);
int basic_pop_value_num();

#endif
