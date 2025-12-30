// TODO: Memory leaks everywhere
#include "basic.h"
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include "arena.h"

basic_interpreter *global_interpreter = NULL;
arena *interpreter_arena = NULL;
jmp_buf err_jmp;
int error_line = 0;

#define ERR(msg, ...)                                    \
    do {                                                 \
        interpreter_log(msg __VA_OPT__(, ) __VA_ARGS__); \
        error_line = __LINE__;                           \
        longjmp(err_jmp, 1);                             \
    } while (0)

void interpreter_log(const char *fmt, ...) {
    static char buffer[256];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);

    if (*buffer == '\n') {
        global_interpreter->print_fn(buffer);
    } else {
        global_interpreter->append_print_fn(buffer);
    }
}

#define TOKENS    \
    X(SEMICOLON)  \
    X(IDENTIFIER) \
    X(KEYWORD)    \
    X(STRING)     \
    X(NUMBER)     \
    X(PLUS)       \
    X(MINUS)      \
    X(EQUAL)      \
    X(DOT)        \
    X(UNEXPECTED) \
    X(EOF)

#define X(x) TOKEN_##x,
typedef enum { TOKENS } token_type;
#undef X

#define X(x) "TOKEN_" #x,
const char *token_string[] = {TOKENS};
#undef X

#define KEYWORDS \
    X(IF)        \
    X(ELSE)      \
    X(FOR)       \
    X(IN)        \
    X(FUNCTION)  \
    X(END)       \
    X(TRUE)      \
    X(FALSE)

#define X(x) #x,
const char *keywords[] = {"KW_NONE", KEYWORDS};
#undef X

#define X(x) KW_##x,
typedef enum { KW_NONE, KEYWORDS } keyword_type;
#undef X
const size_t keywords_count = sizeof(keywords) / sizeof(keywords[0]);

typedef struct {
    token_type type;
    keyword_type keyword;
    const char *start, *end;
} token;

void print_token(token *t) {
    printf("%s [%.*s]\n", token_string[t->type], (int)(t->end - t->start), t->start);
}

keyword_type get_keyword(token tok) {
    size_t token_len = tok.end - tok.start;
    for (size_t i = 1; i < keywords_count; i++) {
        if (strncmp(keywords[i], tok.start, token_len) == 0) {
            return i;
        }
    }
    return KW_NONE;
}

typedef enum {
    STMT_NOP,
    STMT_FUNCALL,
    STMT_IF,
    STMT_FOR,
    STMT_FOREND,
    STMT_VARIABLE,
} stmt_type;

typedef struct stmt stmt;

typedef struct {
    expr condition;
} stmt_if;

typedef struct {
    const char *variable;
    int min, max, step;
} stmt_for;

typedef struct {
    const char *variable;
    expr value;
} stmt_variable;

struct stmt {
    stmt_type type;
    union {
        stmt_funcall stmt_funcall;
        stmt_if stmt_if;
        stmt_for stmt_for;
        stmt_variable stmt_variable;
    } as;

    stmt *next;
    stmt *jmp;
};

void destroy_interpreter() {
    global_interpreter = NULL;
    printf("Freeing %zu allocated bytes from arena\n", interpreter_arena->ptr);
    arena_free(interpreter_arena);
    interpreter_arena = NULL;
}

#define append(l, x)                                                               \
    do {                                                                           \
        if ((l)->count == (l)->capacity) {                                         \
            (l)->capacity = (l)->capacity == 0 ? 64 : (l)->capacity * 2;           \
            (l)->items = realloc((l)->items, sizeof(*(l)->items) * (l)->capacity); \
        }                                                                          \
        (l)->items[(l)->count++] = (x);                                            \
    } while (0)

token next(const char *input) {
    while (isspace(*input))
        input++;
    token result = {0};
    result.start = input;

    if (*input == '\0') {
        result.type = TOKEN_EOF;
    } else if (*input == ';') {
        result.type = TOKEN_SEMICOLON;
        input++;
    } else if (*input == '"') {
        result.type = TOKEN_STRING;
        input++;
        while (*input && *input != '"')
            input++;
        if (*input == '\0') {
            ERR("Mismatching '\"'");
        }
        input++;
    } else if (isalpha(*input) || *input == '$') {
        result.type = TOKEN_IDENTIFIER;
        if (*input == '$')
            input++;
        while (isalnum(*input))
            input++;
    } else if (isdigit(*input)) {
        result.type = TOKEN_NUMBER;
        while (isdigit(*input))
            input++;
    } else if (*input == '+') {
        result.type = TOKEN_PLUS;
        input++;
    } else if (*input == '-') {
        result.type = TOKEN_MINUS;
        input++;
    } else if (*input == '=') {
        result.type = TOKEN_EQUAL;
        input++;
    } else if (*input == '.') {
        result.type = TOKEN_DOT;
        input++;
    } else {
        result.type = TOKEN_UNEXPECTED;
        input++;
    }
    result.end = input;

    if (result.type == TOKEN_IDENTIFIER) {
        keyword_type kw = get_keyword(result);
        if (kw != KW_NONE) {
            result.type = TOKEN_KEYWORD;
            result.keyword = kw;
        }
    }

    return result;
}

#define MAX_TOKENS 2048
token tokens[MAX_TOKENS] = {0};
size_t token_count = 0;
size_t parser_reader = 0;

void lexical_analysis(const char *input) {
    while (1) {
        token tok = next(input);
        tokens[token_count++] = tok;
        if (token_count == MAX_TOKENS) {
            ERR("Program is too big... (%d tokens max)\n", MAX_TOKENS);
        }
        input = tok.end;
        if (tok.type == TOKEN_UNEXPECTED || tok.type == TOKEN_EOF)
            break;
    }
}

token *parser_peek() {
    return &tokens[parser_reader];
}

bool peek_type(token_type type) {
    return parser_peek()->type == type;
}

bool peek_kw(keyword_type type) {
    return parser_peek()->keyword == type;
}

token *parser_next() {
    token *result = parser_peek();
    parser_reader++;
    return result;
}

token *expect(token_type type) {
    token *read = parser_next();
    if (read->type != type) {
        ERR("Expecting token type %s but got %s\n", token_string[type], token_string[read->type]);
    }
    return read;
}

bool expect_kw(keyword_type type) {
    token *read = parser_peek();
    if (read->keyword != type) {
        ERR("Expecting keyword %s but got %s : ", keywords[type], keywords[read->keyword]);
    }
    parser_reader++;
    return true;
}

bool match(token_type type) {
    if (parser_peek()->type == type) {
        parser_reader++;
        return true;
    }
    return false;
}

const char *tok_to_str(token *tok) {
    const char *start = tok->start;
    size_t len = tok->end - tok->start;

    if (tok->type == TOKEN_STRING) {
        start++;
        len -= 2;
    }

    char *result = arena_alloc(interpreter_arena, len + 1);
    memset(result, 0, len + 1);
    strncpy(result, start, len);
    return result;
}

int tok_to_num(token *tok) {
    if (tok->type != TOKEN_NUMBER) {
        ERR("Trying to convert from not a number to a number");
    }
    int result = 0;
    const char *str = tok->start;
    for (; str != tok->end; str++) {
        result *= 10;
        result += ((*str) - '0');
    }
    return result;
}

stmt *parse_block();
expr parse_expr();

stmt *stmt_tail(stmt *s) {
    while (s && s->next) {
        s = s->next;
    }
    return s;
}

stmt *parse_funcall(token *function) {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_FUNCALL;
    result->next = NULL;
    result->jmp = NULL;

    stmt_funcall *funcall = &result->as.stmt_funcall;
    funcall->function = tok_to_str(function);
    funcall->items = NULL;
    funcall->capacity = 0;
    funcall->count = 0;

    while (!peek_type(TOKEN_SEMICOLON) && !peek_type(TOKEN_UNEXPECTED) && !peek_type(TOKEN_EOF)) {
        expr e = parse_expr();
        append(funcall, e);
    }

    expect(TOKEN_SEMICOLON);
    return result;
}

stmt *parse_variable(token *variable) {
    parser_next();  // Skipping =
    expr e = parse_expr();
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_VARIABLE;
    result->as.stmt_variable.variable = tok_to_str(variable);
    result->as.stmt_variable.value = e;
    result->next = NULL;
    result->jmp = NULL;
    expect(TOKEN_SEMICOLON);
    return result;
}

stmt *parse_identifier() {
    token *identifier = parser_next();
    token *next = parser_peek();

    if (next->type == TOKEN_EQUAL) {
        return parse_variable(identifier);
    } else {
        return parse_funcall(identifier);
    }
}

expr parse_expr() {
    if (peek_kw(KW_TRUE)) {
        parser_reader++;
        return (expr){EXPR_BOOL, .as.boolean = 1};
    }
    if (peek_kw(KW_FALSE)) {
        parser_reader++;
        return (expr){EXPR_BOOL, .as.boolean = 0};
    }
    if (peek_type(TOKEN_STRING)) {
        token *tok = expect(TOKEN_STRING);
        return (expr){EXPR_STRING, .as.string = tok_to_str(tok)};
    }
    if (peek_type(TOKEN_IDENTIFIER)) {
        token *tok = expect(TOKEN_IDENTIFIER);
        return (expr){EXPR_VAR, .as.variable = tok_to_str(tok)};
    }
    if (peek_type(TOKEN_NUMBER)) {
        token *tok = expect(TOKEN_NUMBER);
        return (expr){EXPR_NUMBER, .as.number = tok_to_num(tok)};
    }
    ERR("Expected expression\n");
}

stmt *parse_if() {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_IF;
    result->next = NULL;
    result->jmp = NULL;

    stmt_if *if_stmt = &result->as.stmt_if;
    if_stmt->condition = parse_expr();
    expect(TOKEN_SEMICOLON);

    stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
    end->type = STMT_NOP;
    end->next = NULL;
    end->jmp = NULL;

    result->next = parse_block();
    stmt_tail(result->next)->next = end;

    token *peek = parser_peek();
    if (peek->keyword == KW_ELSE) {
        parser_reader++;
        result->jmp = parse_block();
        stmt_tail(result->jmp)->next = end;
    } else {
        result->jmp = end;
    }

    expect_kw(KW_END);
    while (peek_type(TOKEN_SEMICOLON))
        parser_reader++;

    return result;
}

stmt *parse_for() {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_FOR;
    result->next = NULL;
    result->jmp = NULL;

    stmt_for *for_stmt = &result->as.stmt_for;
    token *variable = expect(TOKEN_IDENTIFIER);
    for_stmt->variable = tok_to_str(variable);

    expect_kw(KW_IN);
    token *from = expect(TOKEN_NUMBER);
    expect(TOKEN_DOT);
    expect(TOKEN_DOT);
    token *to = expect(TOKEN_NUMBER);
    expect(TOKEN_SEMICOLON);

    for_stmt->min = tok_to_num(from);
    for_stmt->max = tok_to_num(to);
    for_stmt->step = for_stmt->min > for_stmt->max ? -1 : 1;

    stmt *body = parse_block();

    stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
    end->type = STMT_FOREND;
    end->next = NULL;
    end->jmp = result;
    end->as.stmt_for = result->as.stmt_for;

    if (body) {
        stmt_tail(body)->next = end;
        result->jmp = body;
    } else {
        result->jmp = end;
    }
    result->next = end;

    expect_kw(KW_END);
    return result;
}

stmt *parse_keyword() {
    token *t = expect(TOKEN_KEYWORD);
    switch (t->keyword) {
        case KW_NONE:
            ERR("Unexpected KW_NONE\n");
        case KW_IF:
            return parse_if();
        case KW_FOR:
            return parse_for();
        case KW_FUNCTION:
            ERR("TODO %s\n", keywords[t->keyword]);
        case KW_ELSE:
        case KW_IN:
        case KW_END:
            ERR("Unexpected %s\n", keywords[t->keyword]);
        case KW_TRUE:
        case KW_FALSE:
            break;
    }
    return NULL;
}

stmt *parse_stmt() {
    while (peek_type(TOKEN_SEMICOLON))
        parser_reader++;
    if (peek_type(TOKEN_IDENTIFIER))
        return parse_identifier();
    if (peek_type(TOKEN_KEYWORD))
        return parse_keyword();
    ERR("Unexpected token of type %s\n", token_string[parser_peek()->type]);
}

stmt *parse_block() {
    stmt *head = NULL;
    stmt *tail = NULL;
    while (parser_peek()->type != TOKEN_EOF && parser_peek()->type != TOKEN_UNEXPECTED &&
           parser_peek()->keyword != KW_ELSE && parser_peek()->keyword != KW_END) {
        stmt *s = parse_stmt();
        stmt *s_tail = stmt_tail(s);
        if (head == NULL) {
            head = s;
            tail = s_tail;
        } else {
            tail->next = s;
            tail = s_tail;
        }
    }
    return head;
}

symbol *get_symbol(const char *name) {
    for (size_t i = 0; i < MAX_SYMBOL_COUNT; i++) {
        symbol *s = &global_interpreter->symbols_table[i];
        if (s && s->type != SYMBOL_NONE && strcmp(s->name, name) == 0) {
            return &global_interpreter->symbols_table[i];
        }
    }
    return NULL;
}

symbol *create_symbol(const char *name, symbol_type type) {
    if (get_symbol(name) != NULL) {
        ERR("Symbol %s already exists", name);
    }
    for (size_t i = 0; i < MAX_SYMBOL_COUNT; i++) {
        symbol *loop_variable = &global_interpreter->symbols_table[i];
        if (loop_variable->type == SYMBOL_NONE) {
            loop_variable->name = name;
            loop_variable->type = type;
            global_interpreter->symbol_count++;
            return loop_variable;
        }
    }
    ERR("No more space to allocate more symbols");
}

value eval_expr(expr *e);

void print_val(value *v) {
    if (v->type == VAL_STRING) {
        interpreter_log("%s", v->as.string);
    } else if (v->type == VAL_NUM) {
        interpreter_log("%d", v->as.number);
    } else if (v->type == VAL_BOOL) {
        interpreter_log("%s", v->as.boolean != 0 ? "TRUE" : "FALSE");
    }
}

void print_fn(stmt_funcall *call) {
    for (int i = 0; i < call->count - 1; i++) {
        value v = eval_expr(&call->items[i]);
        print_val(&v);
    }
    value last = eval_expr(&call->items[call->count - 1]);
    print_val(&last);
    interpreter_log("\n");
}

void exit_fn(stmt_funcall *call) {
    (void)call;
    longjmp(err_jmp, -1);
}

void sleep_fn(stmt_funcall *call) {
    value v = eval_expr(&call->items[0]);
    global_interpreter->wakeup_time = global_interpreter->time_elapsed + v.as.number;
    global_interpreter->state = STATE_SLEEPING;
}

void call_function(stmt *s) {
    stmt_funcall call = s->as.stmt_funcall;
    symbol *function = get_symbol(call.function);
    if (function == NULL) {
        ERR("Unknow function '%s'\n", call.function);
    }
    if (function->type != SYMBOL_FUNCTION) {
        ERR("%s is not a function\n", call.function);
    }
    function->as.function(&call);
}

value eval_expr(expr *expr) {
    if (expr->type == EXPR_BOOL) {
        return (value){VAL_BOOL, .as.boolean = expr->as.boolean};
    }
    if (expr->type == EXPR_STRING) {
        return (value){VAL_STRING, .as.string = expr->as.string};
    }
    if (expr->type == EXPR_NUMBER) {
        return (value){VAL_NUM, .as.number = expr->as.number};
    }
    if (expr->type == EXPR_VAR) {
        symbol *s = get_symbol(expr->as.variable);
        if (s == NULL) {
            ERR("Unknown symbol %s", expr->as.variable);
        }
        if (s->type == SYMBOL_VARIABLE_INT) {
            return (value){VAL_NUM, .as.number = s->as.integer};
        } else if (s->type == SYMBOL_VARIABLE_STRING) {
            return (value){VAL_STRING, .as.string = s->as.string};
        }
    }
    ERR("Can't parse this expression\n");
}

bool is_true(value v) {
    if (v.type == VAL_BOOL) {
        return v.as.boolean != 0;
    }
    if (v.type == VAL_NUM) {
        return v.as.number > 0;
    }
    if (v.type == VAL_STRING) {
        return strlen(v.as.string) != 0;
    }
    ERR("Unknown value type");
}

void register_function(basic_interpreter *i, const char *name, void (*f)(stmt_funcall *)) {
    i->symbols_table[i->symbol_count].type = SYMBOL_FUNCTION;
    i->symbols_table[i->symbol_count].name = name;
    i->symbols_table[i->symbol_count].as.function = f;
    i->symbol_count++;
}

void register_std_lib(basic_interpreter *i) {
    register_function(i, "PRINT", print_fn);
    register_function(i, "EXIT", exit_fn);
    register_function(i, "SLEEP", sleep_fn);
}

void init_interpreter(basic_interpreter *i, const char *src) {
    global_interpreter = i;
    interpreter_arena = arena_default();
    parser_reader = 0;
    token_count = 0;
    volatile int error_code = 0;
    if ((error_code = setjmp(err_jmp)) != 0) {
        if (error_code != -1) {
            printf("\nexit from error from line %d\n", error_line);
        }
        destroy_interpreter();
        return;
    }
    register_std_lib(i);
    lexical_analysis(src);
    i->pc = parse_block();
    expect(TOKEN_EOF);
}

bool step_program(basic_interpreter *i) {
    int error_code = 0;
    if ((error_code = setjmp(err_jmp)) != 0) {
        if (error_code != -1) {
            printf("\nexit from error from line %d\n", error_line);
        }
        destroy_interpreter();
        return false;
    }

    if (i->state == STATE_SLEEPING) {
        if (i->time_elapsed >= i->wakeup_time) {
            i->state = STATE_RUNNING;
        } else {
            return true;
        }
    }

    stmt *s = i->pc;
    if (s == NULL) {
        destroy_interpreter();
        return false;
    }
    switch (s->type) {
        case STMT_FUNCALL:
            call_function(s);
            i->pc = s->next;
            break;
        case STMT_IF:
            if (is_true(eval_expr(&s->as.stmt_if.condition))) {
                i->pc = s->next;
            } else {
                i->pc = s->jmp;
            }
            break;
        case STMT_FOR: {
            symbol *var = get_symbol(s->as.stmt_for.variable);
            if (var == NULL) {
                var = create_symbol(s->as.stmt_for.variable, SYMBOL_VARIABLE_INT);
                var->as.integer = s->as.stmt_for.min;
            }
            if (var->as.integer == s->as.stmt_for.max) {
                i->pc = s->next;
            } else {
                i->pc = s->jmp;
            }
            break;
        }
        case STMT_FOREND: {
            symbol *var = get_symbol(s->as.stmt_for.variable);
            if (var->as.integer == s->as.stmt_for.max) {
                i->pc = s->next;
                var->type = SYMBOL_NONE;
                break;
            }
            var->as.integer += s->as.stmt_for.step;
            i->pc = s->jmp;
            break;
        }
        case STMT_NOP:
            i->pc = s->next;
            break;
        case STMT_VARIABLE:
            const char *name = s->as.stmt_variable.variable;
            value v = eval_expr(&s->as.stmt_variable.value);
            symbol *var = get_symbol(name);
            if (var) {
                if (var->type == SYMBOL_FUNCTION) {
                    ERR("Trying to override function %s as a variable", name);
                }
            } else {
                var = create_symbol(name, SYMBOL_VARIABLE_INT);
            }
            var->as.integer = v.as.number;
            i->pc = s->next;
            break;
    }

    return true;
}

void default_print(const char *text) {
    printf("%s", text);
}

void advance_interpreter_time(basic_interpreter *i, float time) {
    i->time_elapsed += time;
}

long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

#ifdef BASIC_TEST
int main(void) {
    const char content[] = {
#embed "../assets/machines_impl/machine1/files/x"
    };
    basic_interpreter i = {.print_fn = default_print, .append_print_fn = default_print};
    init_interpreter(&i, content);

    long long last_time = timeInMilliseconds();
    while (true) {
        long long new_time = timeInMilliseconds();
        advance_interpreter_time(&i, (new_time - last_time) / 1000.f);
        if (!step_program(&i))
            break;
        last_time = new_time;
    }
    return 0;
}
#endif
