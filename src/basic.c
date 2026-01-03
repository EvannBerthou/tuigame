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

#define X(x) "TOKEN_" #x,
const char *token_string[] = {TOKENS};
#undef X

#define X(x) #x,
const char *keywords[] = {"KW_NONE", KEYWORDS};
#undef X
const size_t keywords_count = sizeof(keywords) / sizeof(keywords[0]);

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

void print_token(token *t) {
    printf("%s [%.*s]\n", token_string[t->type], (int)(t->end - t->start), t->start);
}

keyword_type get_keyword(token tok) {
    size_t token_len = tok.end - tok.start;
    for (size_t i = 1; i < keywords_count; i++) {
        if (strncmp(keywords[i], tok.start, token_len) == 0) {
            if (strlen(keywords[i]) == token_len) {
                return i;
            }
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
    STMT_WHILE,
    STMT_WHILEEND,
    STMT_VARIABLE,
    STMT_FUNCDECL,
    STMT_RETURN,
} stmt_type;

typedef struct stmt stmt;

typedef struct {
    expr condition;
} stmt_if;

typedef struct {
    const char *variable;
    int min, max, step;
    size_t stack_saved;
} stmt_for;

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

struct stmt {
    stmt_type type;
    union {
        stmt_funcall stmt_funcall;
        stmt_if stmt_if;
        stmt_for stmt_for;
        stmt_while stmt_while;
        stmt_variable stmt_variable;
        stmt_funcdecl stmt_funcdecl;
    } as;

    stmt *next;
    stmt *jmp;
};

void destroy_interpreter() {
    global_interpreter = NULL;
    arena_free(interpreter_arena);
    interpreter_arena = NULL;
}

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
    } else if (isalpha(*input) || *input == '$' || *input == '_') {
        result.type = TOKEN_IDENTIFIER;
        if (*input == '$')
            input++;
        while (isalnum(*input) || *input == '_')
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
        if (*input && *input == '=') {
            result.type = TOKEN_EQEQ;
            input++;
        }
    } else if (*input == '.') {
        result.type = TOKEN_DOT;
        input++;
    } else if (*input == '*') {
        result.type = TOKEN_STAR;
        input++;
    } else if (*input == '/') {
        result.type = TOKEN_SLASH;
        input++;
    } else if (*input == '(') {
        result.type = TOKEN_LPAREN;
        input++;
    } else if (*input == ')') {
        result.type = TOKEN_RPAREN;
        input++;
    } else if (*input == '!') {
        input++;
        if (*input && *input == '=') {
            result.type = TOKEN_NEQ;
        } else {
            result.type = TOKEN_NOT;
        }
        input++;
    } else if (*input == '>') {
        input++;
        if (*input && *input == '=') {
            result.type = TOKEN_GTE;
        } else {
            result.type = TOKEN_GT;
        }
        input++;
    } else if (*input == '<') {
        input++;
        if (*input && *input == '=') {
            result.type = TOKEN_LTE;
        } else {
            result.type = TOKEN_LT;
        }
        input++;
    } else {
        result.type = TOKEN_UNEXPECTED;
        input++;
    }
    result.end = input;

    if (result.type == TOKEN_IDENTIFIER) {
        if (strncmp(result.start, "AND", 3) == 0) {
            result.type = TOKEN_AND;
        } else if (strncmp(result.start, "OR", 2) == 0) {
            result.type = TOKEN_OR;
        }
    }

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
        ERR("Expecting token type %s but got %s", token_string[type], token_string[read->type]);
    }
    return read;
}

token *expect_kw(keyword_type type) {
    token *read = parser_peek();
    if (read->keyword != type) {
        ERR("Expecting keyword %s but got %s : ", keywords[type], keywords[read->keyword]);
    }
    parser_reader++;
    return read;
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
    expr e = parse_expr();
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_VARIABLE;
    result->as.stmt_variable.variable = tok_to_str(variable);
    result->as.stmt_variable.expr = e;
    result->next = NULL;
    result->jmp = NULL;
    expect(TOKEN_SEMICOLON);
    return result;
}

stmt *parse_identifier() {
    token *identifier = parser_next();
    token *next = parser_peek();

    if (next->type == TOKEN_EQUAL) {
        parser_next();  // Skipping =
        return parse_variable(identifier);
    } else {
        return parse_funcall(identifier);
    }
}

expr_binary *make_binary(token_type op, expr left, expr right) {
    expr_binary *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->op = op;
    result->left = arena_alloc(interpreter_arena, sizeof(*result->left));
    *result->left = left;
    result->right = arena_alloc(interpreter_arena, sizeof(*result->left));
    *result->right = right;
    return result;
}

expr_unary *make_unary(token_type op, expr e) {
    expr_unary *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->op = op;
    result->expr = arena_alloc(interpreter_arena, sizeof(*result->expr));
    *result->expr = e;
    return result;
}

expr parse_primary() {
    if (peek_kw(KW_TRUE)) {
        parser_reader++;
        return (expr){EXPR_NUMBER, .as.number = 1};
    }
    if (peek_kw(KW_FALSE)) {
        parser_reader++;
        return (expr){EXPR_NUMBER, .as.number = 0};
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
    if (peek_type(TOKEN_LPAREN)) {
        expect(TOKEN_LPAREN);
        expr e = parse_expr();
        expect(TOKEN_RPAREN);
        return e;
    }
    ERR("Expected expression\n");
}

expr parse_unary() {
    if (peek_type(TOKEN_MINUS)) {
        expect(TOKEN_MINUS);
        expr right = parse_unary();
        return (expr){EXPR_UNARY, .as.unary = make_unary(TOKEN_MINUS, right)};
    }
    if (peek_type(TOKEN_PLUS)) {
        expect(TOKEN_PLUS);
        expr right = parse_unary();
        return (expr){EXPR_UNARY, .as.unary = make_unary(TOKEN_PLUS, right)};
    }
    return parse_primary();
}

expr parse_mult() {
    expr left = parse_unary();
    while (peek_type(TOKEN_STAR) || peek_type(TOKEN_SLASH)) {
        token *op = parser_next();
        expr right = parse_unary();
        left = (expr){EXPR_BINARY, .as.binary = make_binary(op->type, left, right)};
    }
    return left;
}

expr parse_add() {
    expr left = parse_mult();
    while (peek_type(TOKEN_PLUS) || peek_type(TOKEN_MINUS)) {
        token *op = parser_next();
        expr right = parse_mult();
        left = (expr){EXPR_BINARY, .as.binary = make_binary(op->type, left, right)};
    }
    return left;
}

expr parse_comparaisons() {
    expr left = parse_add();

    while (peek_type(TOKEN_EQEQ) || peek_type(TOKEN_NEQ) || peek_type(TOKEN_LT) || peek_type(TOKEN_LTE) ||
           peek_type(TOKEN_GT) || peek_type(TOKEN_GTE)) {
        token *op = parser_next();
        expr right = parse_add();

        left = (expr){EXPR_BINARY, .as.binary = make_binary(op->type, left, right)};
    }

    return left;
}

expr parse_and() {
    expr left = parse_comparaisons();

    while (peek_type(TOKEN_AND)) {
        token *op = expect(TOKEN_AND);
        expr right = parse_comparaisons();
        left = (expr){EXPR_BINARY, .as.binary = make_binary(op->type, left, right)};
    }

    return left;
}

expr parse_or() {
    expr left = parse_and();

    while (peek_type(TOKEN_OR)) {
        token *op = expect(TOKEN_OR);
        expr right = parse_and();
        left = (expr){EXPR_BINARY, .as.binary = make_binary(op->type, left, right)};
    }

    return left;
}

expr parse_expr() {
    return parse_or();
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

stmt *parse_while() {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_WHILE;
    result->next = NULL;
    result->jmp = NULL;

    stmt_while *while_stmt = &result->as.stmt_while;
    while_stmt->condition = parse_expr();
    expect(TOKEN_SEMICOLON);

    stmt *body = parse_block();

    stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
    end->type = STMT_WHILEEND;
    end->next = NULL;
    end->jmp = result;
    end->as.stmt_while = result->as.stmt_while;

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

stmt *parse_funcdecl() {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_FUNCDECL;
    result->next = NULL;
    result->jmp = NULL;

    stmt_funcdecl *fundecl_stmt = &result->as.stmt_funcdecl;
    token *func_name = expect(TOKEN_IDENTIFIER);
    fundecl_stmt->name = tok_to_str(func_name);
    fundecl_stmt->args.items = NULL;
    fundecl_stmt->args.count = 0;
    fundecl_stmt->args.capacity = 0;
    expect(TOKEN_LPAREN);
    while (peek_type(TOKEN_IDENTIFIER)) {
        token *arg = expect(TOKEN_IDENTIFIER);
        append(&fundecl_stmt->args, tok_to_str(arg));
    }
    expect(TOKEN_RPAREN);
    expect(TOKEN_SEMICOLON);

    stmt *body = parse_block();

    stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
    end->type = STMT_NOP;
    end->next = NULL;
    end->jmp = NULL;

    stmt *return_stmt = arena_alloc(interpreter_arena, sizeof(*return_stmt));
    return_stmt->type = STMT_RETURN;
    return_stmt->next = end;
    return_stmt->jmp = NULL;

    stmt_tail(body)->next = return_stmt;

    result->next = body;
    result->jmp = end;

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
        case KW_FUNC:
            return parse_funcdecl();
        case KW_WHILE:
            return parse_while();
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
    for (int i = global_interpreter->symbol_count - 1; i >= 0; i--) {
        symbol *s = &global_interpreter->symbols_table[i];
        if (s && s->type != SYMBOL_NONE && strcmp(s->name, name) == 0) {
            if (s->scope == SCOPE_GLOBAL ||
                (s->scope == SCOPE_LOCAL && s->scope_depth == global_interpreter->scope_depth)) {
                return &global_interpreter->symbols_table[i];
            }
        }
    }

    return NULL;
}

symbol *get_symbol_id(size_t idx) {
    return &global_interpreter->symbols_table[idx];
}

size_t create_symbol(const char *name, symbol_type type) {
    if (global_interpreter->symbol_count == MAX_SYMBOL_COUNT) {
        ERR("No more space to allocate more symbols");
    }
    symbol *s = &global_interpreter->symbols_table[global_interpreter->symbol_count];
    s->name = name;
    s->type = type;
    return global_interpreter->symbol_count++;
}

value eval_expr(expr *e);

void print_val(value *v) {
    if (v->type == VAL_STRING) {
        interpreter_log("%s", v->as.string);
    } else if (v->type == VAL_NUM) {
        interpreter_log("%d", v->as.number);
    } else {
        ERR("Unknown value type %d\n", v->type);
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

value value_is_num(value v) {
    if (v.type != VAL_NUM) {
        ERR("Trying to do arithmetics on non number elements");
    }
    return v;
}

value eval_expr(expr *expr) {
    switch (expr->type) {
        case EXPR_STRING:
            return (value){VAL_STRING, .as.string = expr->as.string};
        case EXPR_NUMBER:
            return (value){VAL_NUM, .as.number = expr->as.number};
        case EXPR_VAR:
            symbol *s = get_symbol(expr->as.variable);
            if (s == NULL) {
                ERR("Unknown symbol %s", expr->as.variable);
            }
            if (s->type == SYMBOL_VARIABLE_INT) {
                return (value){VAL_NUM, .as.number = s->as.integer};
            } else if (s->type == SYMBOL_VARIABLE_STRING) {
                return (value){VAL_STRING, .as.string = s->as.string};
            }
            break;
        case EXPR_BINARY: {
            expr_binary *bin = expr->as.binary;

            if (bin->op == TOKEN_AND) {
                value left_v = value_is_num(eval_expr(bin->left));
                if (left_v.as.number == 0)
                    return left_v;
                return value_is_num(eval_expr(bin->right));
            } else if (bin->op == TOKEN_OR) {
                value left_v = value_is_num(eval_expr(bin->left));
                if (left_v.as.number != 0)
                    return left_v;
                return value_is_num(eval_expr(bin->right));
            } else {
                value left_v = value_is_num(eval_expr(bin->left));
                value right_v = value_is_num(eval_expr(bin->right));
                int left = left_v.as.number;
                int right = right_v.as.number;
                int result = 0;
                if (bin->op == TOKEN_PLUS)
                    result = left + right;
                else if (bin->op == TOKEN_MINUS)
                    result = left - right;
                else if (bin->op == TOKEN_STAR)
                    result = left * right;
                else if (bin->op == TOKEN_SLASH)
                    result = left / right;
                else if (bin->op == TOKEN_EQEQ)
                    result = left == right;
                else if (bin->op == TOKEN_NEQ)
                    result = left != right;
                else if (bin->op == TOKEN_LT)
                    result = left < right;
                else if (bin->op == TOKEN_LTE)
                    result = left <= right;
                else if (bin->op == TOKEN_GT)
                    result = left > right;
                else if (bin->op == TOKEN_GTE)
                    result = left >= right;
                else
                    ERR("Unknown binary operation %s between %d and %d\n", token_string[bin->op], left, right);
                return (value){VAL_NUM, .as.number = result};
            }
        }
        case EXPR_UNARY:
            expr_unary *unary = expr->as.unary;
            if (unary->op == TOKEN_MINUS) {
                return (value){VAL_NUM, .as.number = -eval_expr(unary->expr).as.number};
            }
            if (unary->op == TOKEN_PLUS) {
                return eval_expr(unary->expr);
            }
            break;
    }

    ERR("Can't evaluate this expression\n");
}

bool is_true(value v) {
    if (v.type == VAL_NUM) {
        return v.as.number != 0;
    }
    if (v.type == VAL_STRING) {
        return strlen(v.as.string) != 0;
    }
    ERR("Unknown value type");
}

void register_function(const char *name, void (*f)(stmt_funcall *), int arg_count) {
    symbol *s = get_symbol_id(create_symbol(name, SYMBOL_FUNCTION_NATIVE));
    s->as.native_func.function = f;
    if (arg_count < 0) {
        s->as.native_func.arg_count = 0;
        s->as.native_func.variadic_arg_count = true;
    } else {
        s->as.native_func.arg_count = arg_count;
    }
}

void register_variable_int(const char *name, int value) {
    symbol *s = get_symbol_id(create_symbol(name, SYMBOL_VARIABLE_INT));
    s->scope_depth = 0;
    s->scope = SCOPE_GLOBAL;
    s->as.integer = value;
}

void register_variable_string(const char *name, const char *value) {
    symbol *s = get_symbol_id(create_symbol(name, SYMBOL_VARIABLE_STRING));
    s->scope_depth = 0;
    s->scope = SCOPE_GLOBAL;
    s->as.string = value;
}

void register_std_lib() {
    register_function("PRINT", print_fn, -1);
    register_function("EXIT", exit_fn, 1);
    register_function("SLEEP", sleep_fn, 1);
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
    register_std_lib();
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
        case STMT_FUNCALL: {
            stmt_funcall call = s->as.stmt_funcall;
            symbol *function = get_symbol(call.function);
            if (function == NULL) {
                ERR("Unknow function '%s'\n", call.function);
            }
            switch (function->type) {
                case SYMBOL_FUNCTION_NATIVE: {
                    // TODO: Args
                    if ((size_t)s->as.stmt_funcall.count != function->as.native_func.arg_count &&
                        function->as.native_func.variadic_arg_count == false) {
                        ERR("Function %s expected %d arguments but recieved %d", function->name,
                            function->as.funcdecl.arg_count, s->as.stmt_funcall.count);
                    }
                    function->as.native_func.function(&call);
                    i->pc = s->next;
                    break;
                }
                case SYMBOL_FUNCTION: {
                    int function_arg_count = function->as.funcdecl.arg_count;
                    if (s->as.stmt_funcall.count != function_arg_count) {
                        ERR("Function %s expected %d arguments but recieved %d", function->name,
                            function->as.funcdecl.arg_count, s->as.stmt_funcall.count);
                    }

                    size_t saved = interpreter_arena->ptr;
                    symbol *args = arena_alloc(interpreter_arena, sizeof(*args) * function->as.funcdecl.arg_count);
                    for (int i = 0; i < function_arg_count; i++) {
                        value v = eval_expr(&s->as.stmt_funcall.items[i]);
                        switch (v.type) {
                            case VAL_NUM:
                                args[i].type = SYMBOL_VARIABLE_INT;
                                args[i].as.integer = v.as.number;
                                break;
                            case VAL_STRING:
                                args[i].type = SYMBOL_VARIABLE_STRING;
                                args[i].as.string = v.as.string;
                                break;
                        }
                    }

                    size_t pop_idx = global_interpreter->symbol_count;
                    global_interpreter->scope_depth++;

                    for (int i = 0; i < function_arg_count; i++) {
                        symbol *new = get_symbol_id(create_symbol(function->as.funcdecl.args[i], args[i].type));
                        new->scope = SCOPE_LOCAL;
                        new->scope_depth = global_interpreter->scope_depth;
                        switch (new->type) {
                            case SYMBOL_VARIABLE_INT:
                                new->as.integer = args[i].as.integer;
                                break;
                            case SYMBOL_VARIABLE_STRING:
                                new->as.string = args[i].as.string;
                                break;
                            default:
                                ERR("Unexpected type %d for fonction argument", new->type);
                        }
                    }

                    return_call r = {s->next, pop_idx};
                    append(&i->returns, r);
                    i->pc = function->as.funcdecl.body;

                    interpreter_arena->ptr = saved;
                    break;
                }
                default:
                    ERR("%s is not a function\n", call.function);
            }
            break;
        }
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
                s->as.stmt_for.stack_saved = global_interpreter->symbol_count;
                var = get_symbol_id(create_symbol(s->as.stmt_for.variable, SYMBOL_VARIABLE_INT));
                var->as.integer = s->as.stmt_for.min;
                var->scope = SCOPE_LOCAL;
                var->scope_depth = global_interpreter->scope_depth;
            }
            if (var->as.integer == s->as.stmt_for.max) {
                i->pc = s->next;
                global_interpreter->symbol_count = s->as.stmt_for.stack_saved;
            } else {
                i->pc = s->jmp;
            }
            break;
        }
        case STMT_FOREND: {
            symbol *var = get_symbol(s->as.stmt_for.variable);
            if (var == NULL) {
                i->pc = s->next;
            } else {
                var->as.integer += s->as.stmt_for.step;
                i->pc = s->jmp;
            }
            break;
        }
        case STMT_NOP:
            i->pc = s->next;
            break;
        case STMT_VARIABLE: {
            const char *name = s->as.stmt_variable.variable;
            value v = eval_expr(&s->as.stmt_variable.expr);
            symbol *var = get_symbol(name);
            if (var) {
                if (var->type == SYMBOL_FUNCTION) {
                    ERR("Trying to override function %s as a variable", name);
                }
            } else {
                if (v.type == VAL_NUM) {
                    var = get_symbol_id(create_symbol(name, SYMBOL_VARIABLE_INT));
                } else if (v.type == VAL_STRING) {
                    var = get_symbol_id(create_symbol(name, SYMBOL_VARIABLE_STRING));
                } else {
                    ERR("Unknown value type %d", v.type);
                }
                var->scope = global_interpreter->scope_depth == 0 ? SCOPE_GLOBAL : SCOPE_LOCAL;
                var->scope_depth = global_interpreter->scope_depth;
            }
            if (v.type == VAL_NUM) {
                var->type = SYMBOL_VARIABLE_INT;
                var->as.integer = v.as.number;
            } else if (v.type == VAL_STRING) {
                var->type = SYMBOL_VARIABLE_STRING;
                var->as.string = v.as.string;
            } else {
                ERR("Unknown symbol type %d", var->type);
            }
            i->pc = s->next;
            break;
        }
        case STMT_WHILE:
        case STMT_WHILEEND: {
            value condition = eval_expr(&s->as.stmt_while.condition);
            if (is_true(condition)) {
                i->pc = s->jmp;
            } else {
                i->pc = s->next;
            }
            break;
        }
        case STMT_FUNCDECL: {
            symbol *var = get_symbol_id(create_symbol(s->as.stmt_funcdecl.name, SYMBOL_FUNCTION));
            var->as.funcdecl.body = s->next;
            var->as.funcdecl.args = s->as.stmt_funcdecl.args.items;
            var->as.funcdecl.arg_count = s->as.stmt_funcdecl.args.count;
            i->pc = s->jmp;
            break;
        }
        case STMT_RETURN: {
            return_call r = pop(&i->returns);
            i->pc = r.return_stmt;
            global_interpreter->symbol_count = r.stack_idx;
            global_interpreter->scope_depth--;
            break;
        }
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
