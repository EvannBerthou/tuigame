/*
 * TODO:
 *   - Array
 *   - Table like in Lua
 *   - Function return statement
 *   - Function return value
 *   - Import another file
 *   - TUIGame system call
 *   - Lots of tests cases
 *   - Comments
 *   - More math functions
 * Bugs:
 *   - Semicolons should be no-op but can cause crashes
 */
#include "basic.h"
#include <ctype.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptrace.h>
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
bool should_go_to_sleep = false;
arena *interpreter_arena = NULL;
jmp_buf err_jmp;
int error_line = 0;

// NOTE: from https://forum.juce.com/t/detecting-if-a-process-is-being-run-under-a-debugger/2098
bool in_debugger() {
    static int is_in_debugger = -1;
    if (is_in_debugger == -1) {
        if (ptrace(PTRACE_TRACEME, 0, 1, 0) < 0) {
            is_in_debugger = 1;
        } else {
            ptrace(PTRACE_DETACH, 0, 1, 0);
            is_in_debugger = 0;
        }
    }
    return is_in_debugger;
}

#if BASIC_TEST
#define BREAKPOINT()         \
    do {                     \
        if (in_debugger()) { \
            asm("int3");     \
        }                    \
    } while (0)
#else
#define BREAKPOINT() ;
#endif

#define ERR(msg, ...)                                    \
    do {                                                 \
        BREAKPOINT();                                    \
        interpreter_log(msg __VA_OPT__(, ) __VA_ARGS__); \
        error_line = __LINE__;                           \
        longjmp(err_jmp, 1);                             \
    } while (0)

#include <stdio.h>
#include <string.h>

void int_to_str(int n, char *result) {
    int i = 0;
    int neg = n < 0;
    if (n < 0) {
        n = -n;
    }

    if (n == 0) {
        result[i++] = '0';
    }

    while (n > 0) {
        result[i++] = n % 10 + '0';
        n /= 10;
    }
    if (neg) {
        result[i++] = '-';
    }
    result[i] = '\0';

    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = result[j];
        result[j] = result[k];
        result[k] = temp;
    }
}

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
symbol *get_symbol(const char *name);

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

    expect(TOKEN_LPAREN);
    while (!peek_type(TOKEN_SEMICOLON) && !peek_type(TOKEN_UNEXPECTED) && !peek_type(TOKEN_EOF) &&
           !peek_type(TOKEN_RPAREN)) {
        expr e = parse_expr();
        append(funcall, e);
    }
    expect(TOKEN_RPAREN);

    symbol *s = get_symbol(funcall->function);
    if (s != NULL && s->type == SYMBOL_FUNCTION_NATIVE) {
        stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
        end->type = STMT_NOP;
        end->next = NULL;
        end->jmp = NULL;

        result->next = end;
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
    if (peek_type(TOKEN_EQUAL)) {
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

expr parse_identifier_expr() {
    token *tok = expect(TOKEN_IDENTIFIER);
    if (peek_type(TOKEN_LPAREN)) {
        expr_funcall result = {0};
        result.name = tok_to_str(tok);

        parser_next();
        while (parser_peek()->type != TOKEN_RPAREN) {
            expr e = parse_expr();
            append(&result.args, e);
        }

        expect(TOKEN_RPAREN);
        return (expr){EXPR_FUNCALL, .as.funcall = result};
    }
    return (expr){EXPR_VAR, .as.variable = tok_to_str(tok)};
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
        return parse_identifier_expr();
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
    ERR("Unexpected %s in expr parsing\n", token_string[parser_peek()->type]);
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

stmt *parse_expr_stmt() {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_EXPR;
    result->next = NULL;
    result->jmp = NULL;

    result->as.stmt_expr.expr = parse_expr();
    expect(TOKEN_SEMICOLON);

    return result;
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

    stmt *body = parse_block();
    if (body != NULL) {
        result->next = body;
        stmt_tail(result->next)->next = end;
    } else {
        result->next = end;
    }

    token *peek = parser_peek();
    if (peek->keyword == KW_ELSE) {
        parser_reader++;
        stmt *else_body = parse_block();
        if (else_body != NULL) {
            result->jmp = else_body;
            stmt_tail(else_body)->next = end;
        } else {
            result->jmp = end;
        }
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
    for_stmt->min = parse_expr();
    expect(TOKEN_DOT);
    expect(TOKEN_DOT);
    for_stmt->max = parse_expr();
    expect(TOKEN_SEMICOLON);

    stmt *body = parse_block();

    stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
    end->type = STMT_FOREND;
    end->next = NULL;
    end->jmp = NULL;
    end->as.stmt_for_end = (stmt_for_end){.min = 0, .max = 0, .variable = for_stmt->variable};

    if (body) {
        stmt_tail(body)->next = end;
        result->next = body;
    } else {
        result->next = end;
    }
    result->jmp = end;
    end->jmp = result->next;

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

    if (body) {
        stmt_tail(body)->next = end;
        result->next = body;
    } else {
        result->next = end;
    }
    result->jmp = end;

    expect_kw(KW_END);
    return result;
}

bool parsing_funcdecl = false;

stmt *parse_funcdecl() {
    if (parsing_funcdecl) {
        ERR("Nested function declaration are not allowed");
    }
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

    stmt *end = arena_alloc(interpreter_arena, sizeof(*end));
    end->type = STMT_NOP;
    end->next = NULL;
    end->jmp = NULL;

    stmt *function_exit_stmt = arena_alloc(interpreter_arena, sizeof(*function_exit_stmt));
    function_exit_stmt->type = STMT_FUNCTION_EXIT;
    function_exit_stmt->next = end;
    function_exit_stmt->jmp = NULL;

    parsing_funcdecl = true;
    stmt *body = parse_block();
    parsing_funcdecl = false;

    stmt *last_stmt = stmt_tail(body);
    if (last_stmt == NULL || last_stmt->type != STMT_RETURN) {
        stmt *return_stmt = arena_alloc(interpreter_arena, sizeof(*return_stmt));
        return_stmt->type = STMT_RETURN;
        return_stmt->next = NULL;
        return_stmt->jmp = NULL;
        return_stmt->as.stmt_return.expr = (expr){.type = EXPR_NUMBER, .as.number = 0};
        if (body == NULL) {
            body = return_stmt;
            last_stmt = return_stmt;
        }
        last_stmt->next = return_stmt;
        last_stmt = return_stmt;
    }
    last_stmt->next = function_exit_stmt;

    stmt *s = body;
    while (s && s != function_exit_stmt) {
        if (s->type == STMT_RETURN) {
            s->next = function_exit_stmt;
        }
        s = s->next;
    }

    result->next = body;
    result->jmp = end;

    expect_kw(KW_END);
    return result;
}

stmt *parse_return() {
    stmt *result = arena_alloc(interpreter_arena, sizeof(*result));
    result->type = STMT_RETURN;
    result->next = NULL;
    result->jmp = NULL;

    result->as.stmt_return.expr = parse_expr();

    expect(TOKEN_SEMICOLON);

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
        case KW_RETURN:
            return parse_return();
        case KW_WHILE:
            return parse_while();
        case KW_ELSE:
        case KW_IN:
        case KW_END:
            ERR("Unexpected keyword %s\n", keywords[t->keyword]);
        case KW_TRUE:
        case KW_FALSE:
            break;
    }
    return NULL;
}

stmt *parse_stmt() {
    while (peek_type(TOKEN_SEMICOLON))
        parser_reader++;
    if (peek_type(TOKEN_EOF))
        return NULL;
    if (peek_type(TOKEN_IDENTIFIER))
        return parse_identifier();
    if (peek_type(TOKEN_KEYWORD))
        return parse_keyword();
    if (peek_type(TOKEN_NUMBER))
        return parse_expr_stmt();
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

void print_val(value *v) {
    if (v->type == VAL_STRING) {
        interpreter_log("%s", v->as.string);
    } else if (v->type == VAL_NUM) {
        interpreter_log("%d", v->as.number);
    } else {
        ERR("Unknown value type %d\n", v->type);
    }
}

void internal_print_fn() {
    expr_frame *top_frame = &global_interpreter->expr_frames.items[global_interpreter->expr_frames.count - 1];
    size_t start = top_frame->value_stack_idx;
    size_t end = global_interpreter->values_stack.count;

    for (size_t i = start; i < end; i++) {
        value v = global_interpreter->values_stack.items[start + i];
        print_val(&v);
    }
    basic_push_int(0);
}

void printn_fn() {
    internal_print_fn();
    interpreter_log("\n");
}

void print_fn() {
    internal_print_fn();
}

void exit_fn() {
    longjmp(err_jmp, -1);
}

void sleep_fn() {
    int v = basic_pop_value_num();
    global_interpreter->wakeup_time = global_interpreter->time_elapsed + v;
    global_interpreter->state = STATE_SLEEPING;
}

void mod_fn() {
    int mod = basic_pop_value_num();
    int v = basic_pop_value_num();
    basic_push_int(fmod(v, mod));
}

void length_fn() {
    const char *s = basic_pop_value_string();
    basic_push_int(strlen(s));
}

void expr_save_frame_resume(continuation c);

void build_expr_plan(expr *expr) {
    if (!expr) {
        ERR("No expression");
        return;
    }
    expr_ops *ops = &global_interpreter->current_expr_plan;
    switch (expr->type) {
        case EXPR_STRING: {
            expr_op op = {OP_PUSH_STRING, .as.string = expr->as.string};
            append(ops, op);
            break;
        }
        case EXPR_NUMBER: {
            expr_op op = {OP_PUSH_NUMBER, .as.number = expr->as.number};
            append(ops, op);
            break;
        }
        case EXPR_VAR: {
            expr_op op = {OP_PUSH_VAR, .as.variable = expr->as.variable};
            append(ops, op);
            break;
        }
        case EXPR_BINARY: {
            if (expr->as.binary->op == TOKEN_OR || expr->as.binary->op == TOKEN_AND) {
                build_expr_plan(expr->as.binary->left);
                expr_op op = {OP_BINARY_OP, .as.op = expr->as.binary->op};
                append(ops, op);
                build_expr_plan(expr->as.binary->right);
            } else {
                build_expr_plan(expr->as.binary->left);
                build_expr_plan(expr->as.binary->right);
                expr_op op = {OP_BINARY_OP, .as.op = expr->as.binary->op};
                append(ops, op);
            }
            break;
        }
        case EXPR_UNARY: {
            build_expr_plan(expr->as.unary->expr);
            expr_op op = {OP_UNARY_OP, .as.op = expr->as.binary->op};
            append(ops, op);
            break;
        }
        case EXPR_FUNCALL: {
            expr_funcall *funcall = &expr->as.funcall;
            for (int i = 0; i < funcall->args.count; i++) {
                build_expr_plan(&funcall->args.items[i]);
            }

            expr_op op = {OP_CALL_FUNC, .as.func = {.name = funcall->name, .argc = funcall->args.count, .stmt = false}};
            append(ops, op);
            break;
        }
        default:
            ERR("Unexpected expr type %d in build_expr_plan", expr->type);
    }
}

void build_stmt_funcall_plan(stmt_funcall *funcall) {
    for (int i = 0; i < funcall->count; i++) {
        build_expr_plan(&funcall->items[i]);
    }

    expr_ops *ops = &global_interpreter->current_expr_plan;
    expr_op op = {OP_CALL_FUNC, .as.func = {.name = funcall->function, .argc = funcall->count, .stmt = true}};
    append(ops, op);
}

void restore_previous_frame() {
    if (global_interpreter->expr_frames.count == 0) {
        ERR("No more frame to restore");
    }
    free(global_interpreter->current_expr_plan.items);
    expr_frame frame = pop(&global_interpreter->expr_frames);
    global_interpreter->current_expr_plan = frame.plan;
    global_interpreter->current_expr_plan_idx = frame.plan_idx;
    global_interpreter->values_stack.count = frame.value_stack_idx;
    global_interpreter->pending_return = frame.pending_return;
}

expr_op *nop_node() {
    expr_op *result = malloc(sizeof(*result));
    result->type = OP_NOP;
    return result;
}

void push_nop() {
    expr_ops *ops = &global_interpreter->current_expr_plan;
    expr_op op = {.type = OP_NOP};
    append(ops, op);
}

void expr_save_frame_new(continuation c) {
    if (global_interpreter->current_expr_plan.count == 0) {
        global_interpreter->current_expr_plan.items = nop_node();
        global_interpreter->current_expr_plan.count = 1;
        global_interpreter->current_expr_plan.capacity = 1;
    }

    expr_frame frame = {.plan = global_interpreter->current_expr_plan,
                        .plan_idx = global_interpreter->current_expr_plan_idx,
                        .value_stack_idx = global_interpreter->values_stack.count,
                        .continuation = c,
                        .own_returns = false,
                        .pending_return = global_interpreter->pending_return};
    append(&global_interpreter->expr_frames, frame);

    global_interpreter->current_expr_plan_idx = 0;
    global_interpreter->current_expr_plan.count = 0;
    global_interpreter->current_expr_plan.capacity = 0;
    global_interpreter->current_expr_plan.items = NULL;
    global_interpreter->pending_return = false;
}

void expr_save_frame_resume(continuation c) {
    if (global_interpreter->current_expr_plan.count == 0) {
        global_interpreter->current_expr_plan.items = nop_node();
        global_interpreter->current_expr_plan.count = 1;
        global_interpreter->current_expr_plan.capacity = 1;
    }

    expr_frame frame = {.plan = global_interpreter->current_expr_plan,
                        .plan_idx = global_interpreter->current_expr_plan_idx + 1,
                        .value_stack_idx = global_interpreter->values_stack.count,
                        .continuation = c,
                        .own_returns = true,
                        .pending_return = global_interpreter->pending_return};
    append(&global_interpreter->expr_frames, frame);

    global_interpreter->current_expr_plan_idx = 0;
    global_interpreter->current_expr_plan.count = 0;
    global_interpreter->current_expr_plan.capacity = 0;
    global_interpreter->current_expr_plan.items = NULL;
    global_interpreter->pending_return = false;
}

void discard_current_frame();

void schedule_function_call(const char *name, size_t argc, symbol *function) {
    size_t expected_arg_count = function->as.funcdecl.arg_count;
    if (expected_arg_count != argc) {
        ERR("Function %s expected %d arguments but recieved %d", name, expected_arg_count, argc);
    }

    global_interpreter->scope_depth++;
    size_t stack_base = global_interpreter->values_stack.count - argc;
    for (size_t i = 0; i < argc; i++) {
        value v = global_interpreter->values_stack.items[stack_base + i];
        const char *var_name = function->as.funcdecl.args[i];
        symbol *s;
        switch (v.type) {
            case VAL_NUM:
                s = get_symbol_id(create_symbol(var_name, SYMBOL_VARIABLE_INT));
                s->as.integer = v.as.number;
                break;
            case VAL_STRING:
                s = get_symbol_id(create_symbol(var_name, SYMBOL_VARIABLE_STRING));
                s->as.string = v.as.string;
                break;
            default:
                ERR("Unknown value type %d\n", v.type);
        }
        s->scope = SCOPE_LOCAL;
        s->scope_depth = global_interpreter->scope_depth;
    }
    global_interpreter->values_stack.count = stack_base;

    global_interpreter->pc = function->as.funcdecl.body;
    global_interpreter->state = STATE_RUNNING;
}

void schedule_expr_function_call(const char *name, size_t argc, symbol *function) {
    size_t saved_stack = global_interpreter->symbol_count;
    schedule_function_call(name, argc, function);
    return_call r = {.return_stmt = NULL, .stack_idx = saved_stack, .is_expr_call = true};
    append(&global_interpreter->returns, r);
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

void discard_current_frame() {
    free(global_interpreter->current_expr_plan.items);
    global_interpreter->current_expr_plan.items = NULL;
    global_interpreter->current_expr_plan.count = 0;
    global_interpreter->current_expr_plan.capacity = 0;
    global_interpreter->current_expr_plan_idx = 0;
}

size_t get_current_value_stack_delta() {
    expr_frame *top_frame = &global_interpreter->expr_frames.items[global_interpreter->expr_frames.count - 1];
    size_t start = top_frame->value_stack_idx;
    size_t end = global_interpreter->values_stack.count;
    if (end < start) {
        ERR("Something went wrong this the value stack");
    }
    size_t arg_count = end - start;
    return arg_count;
}

void eval_continuation() {
    if (global_interpreter->expr_frames.count == 0) {
        ERR("No more frames ?");
    }
    expr_frame frame = global_interpreter->expr_frames.items[global_interpreter->expr_frames.count - 1];
    continuation c = frame.continuation;
    switch (c.type) {
        case CONT_NONE:
            break;
        case CONT_ASSIGN: {
            value v = pop(&global_interpreter->values_stack);
            const char *name = c.as.stmt_assign.variable;
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
            global_interpreter->state = STATE_RUNNING;
            global_interpreter->pc = c.as.stmt_assign.next;
            restore_previous_frame();
            break;
        }
        case CONT_IF: {
            value v = pop(&global_interpreter->values_stack);
            if (is_true(v)) {
                global_interpreter->pc = c.as.stmt_if.if_branch;
            } else {
                global_interpreter->pc = c.as.stmt_if.else_branch;
            }
            restore_previous_frame();
            global_interpreter->state = STATE_RUNNING;
            break;
        }
        case CONT_DISCARD: {
            restore_previous_frame();
            global_interpreter->state = STATE_RUNNING;
            global_interpreter->pc = c.as.stmt_discard.next;
            break;
        }
        case CONT_LOOP_MIN: {
            c.as.cont_stmt_for.end->as.stmt_for_end.min = basic_pop_value_num();
            restore_previous_frame();
            global_interpreter->state = STATE_RUNNING;
            global_interpreter->pc = c.as.cont_stmt_for.end;
            break;
        }
        case CONT_LOOP_MAX: {
            c.as.cont_stmt_for.end->as.stmt_for_end.max = basic_pop_value_num();
            restore_previous_frame();
            global_interpreter->state = STATE_CONTINUE_EXPR_EVALUATION;
            break;
        }
        case CONT_WHILE: {
            if (is_true(pop(&global_interpreter->values_stack))) {
                global_interpreter->pc = c.as.cont_stmt_while.entry;
            } else {
                global_interpreter->pc = c.as.cont_stmt_while.out;
            }
            restore_previous_frame();
            global_interpreter->state = STATE_RUNNING;
            break;
        }
        default:
            ERR("Unknown continuation type");
    }
}

void do_function_exit();

bool step_expr() {
    if ((int)global_interpreter->current_expr_plan_idx >= global_interpreter->current_expr_plan.count) {
        ERR("Reading too much expressions");
    }
    expr_op *expr = &global_interpreter->current_expr_plan.items[global_interpreter->current_expr_plan_idx];
    switch (expr->type) {
        case OP_PUSH_STRING: {
            basic_push_string(expr->as.string);
            global_interpreter->current_expr_plan_idx++;
            break;
        }
        case OP_PUSH_NUMBER: {
            basic_push_int(expr->as.number);
            global_interpreter->current_expr_plan_idx++;
            break;
        }
        case OP_PUSH_VAR: {
            symbol *s = get_symbol(expr->as.variable);
            if (s == NULL) {
                ERR("Unknown symbol %s", expr->as.variable);
            }
            if (s->type == SYMBOL_VARIABLE_INT) {
                basic_push_int(s->as.integer);
                global_interpreter->current_expr_plan_idx++;
                break;
            } else if (s->type == SYMBOL_VARIABLE_STRING) {
                basic_push_string(s->as.string);
                global_interpreter->current_expr_plan_idx++;
                break;
            } else if (s->type == SYMBOL_FUNCTION_NATIVE) {
                s->as.native_func.function();
                global_interpreter->state = STATE_RUNNING;
                global_interpreter->pc = global_interpreter->pc->next;
                global_interpreter->current_expr_plan_idx++;
                break;
            } else if (s->type == SYMBOL_FUNCTION) {
                ERR("Should not be called");
                return true;
            } else {
                ERR("Unexpected symbol type %d in expression", s->type);
            }
            break;
        }
        case OP_BINARY_OP: {
            token_type op = expr->as.op;

            if (op == TOKEN_AND) {
                if (!is_true(pop(&global_interpreter->values_stack))) {
                    basic_push_int(0);
                    global_interpreter->current_expr_plan_idx = global_interpreter->current_expr_plan.count;
                } else {
                    global_interpreter->current_expr_plan_idx++;
                }
            } else if (op == TOKEN_OR) {
                if (is_true(pop(&global_interpreter->values_stack))) {
                    basic_push_int(1);
                    global_interpreter->current_expr_plan_idx = global_interpreter->current_expr_plan.count;
                } else {
                    global_interpreter->current_expr_plan_idx++;
                }
            } else {
                value right_v = pop(&global_interpreter->values_stack);
                value left_v = pop(&global_interpreter->values_stack);
                if (right_v.type == VAL_NUM && left_v.type == VAL_NUM) {
                    int left = left_v.as.number;
                    int right = right_v.as.number;
                    int result = 0;
                    if (op == TOKEN_PLUS)
                        result = left + right;
                    else if (op == TOKEN_MINUS)
                        result = left - right;
                    else if (op == TOKEN_STAR)
                        result = left * right;
                    else if (op == TOKEN_SLASH)
                        result = left / right;
                    else if (op == TOKEN_EQEQ)
                        result = left == right;
                    else if (op == TOKEN_NEQ)
                        result = left != right;
                    else if (op == TOKEN_LT)
                        result = left < right;
                    else if (op == TOKEN_LTE)
                        result = left <= right;
                    else if (op == TOKEN_GT)
                        result = left > right;
                    else if (op == TOKEN_GTE)
                        result = left >= right;
                    else
                        ERR("Unknown binary operation %s between %d and %d\n", token_string[op], left, right);
                    basic_push_int(result);
                    global_interpreter->current_expr_plan_idx++;
                } else {
                    if (op != TOKEN_PLUS) {
                        ERR("Unknown binary operation %s", token_string[op]);
                    }
                    int first_len = value_as_string(left_v, NULL);
                    int second_len = value_as_string(right_v, NULL);
                    int total_len = first_len + second_len + 1;
                    char *concat = arena_alloc(interpreter_arena, total_len);
                    memset(concat, 0, total_len);
                    value_as_string(left_v, concat);
                    value_as_string(right_v, concat + first_len);
                    basic_push_string(concat);
                    global_interpreter->current_expr_plan_idx++;
                }
            }
            break;
        }
        case OP_UNARY_OP: {
            token_type op = expr->as.op;
            int v = basic_pop_value_num();
            if (op == TOKEN_MINUS) {
                basic_push_int(-v);
            } else if (op == TOKEN_PLUS) {
                basic_push_int(v);
            } else {
                ERR("Unknown unary op %s", token_string[op]);
            }
            global_interpreter->current_expr_plan_idx++;
            break;
        }
        case OP_CALL_FUNC: {
            const char *name = expr->as.func.name;
            symbol *function = get_symbol(name);
            if (function == NULL || (function->type != SYMBOL_FUNCTION_NATIVE && function->type != SYMBOL_FUNCTION)) {
                ERR("Unknow function '%s'\n", name);
            }

            if (function->type == SYMBOL_FUNCTION_NATIVE) {
                global_interpreter->scope_depth++;
                function->as.native_func.function();

                if (expr->as.func.stmt) {
                    expr_save_frame_new((continuation){.type = CONT_DISCARD});
                    do_function_exit();
                } else {
                    // If we are inside an expression, we fake the whole return procedure
                    expr_save_frame_new((continuation){.type = CONT_DISCARD});
                    return_call r = {
                        .return_stmt = NULL, .stack_idx = global_interpreter->symbol_count, .is_expr_call = true};
                    append(&global_interpreter->returns, r);
                    do_function_exit();
                    pop(&global_interpreter->values_stack);
                }

                if (global_interpreter->state != STATE_SLEEPING) {
                    global_interpreter->state = STATE_EXPR_EVALUATION;
                }
                global_interpreter->current_expr_plan_idx++;
                break;
            }

            if (expr->as.func.stmt) {
                schedule_function_call(function->name, expr->as.func.argc, function);
            } else {
                schedule_expr_function_call(function->name, expr->as.func.argc, function);
            }

            global_interpreter->current_expr_plan_idx++;
            return true;
        }
        case OP_NOP:
            global_interpreter->current_expr_plan_idx++;
            break;
        default: {
            ERR("Can't evaluate this expression %d\n", expr->type);
            break;
        }
    }
    return false;
}

void register_function(const char *name, void (*f)(), int arg_count) {
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

void breakpoint_fn() {
    BREAKPOINT();
}

void register_std_lib() {
    // DEBUG
    register_function("BP", breakpoint_fn, 0);
    // IO
    register_function("PRINTN", printn_fn, -1);
    register_function("PRINT", print_fn, -1);
    register_function("EXIT", exit_fn, 1);
    register_function("SLEEP", sleep_fn, 1);
    // MATHS
    register_function("MOD", mod_fn, 2);
    // STRINGS
    register_function("LENGTH", length_fn, 1);
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
    i->current_expr_plan_idx = 0;
    i->current_expr_plan.count = 0;
    register_std_lib();
    lexical_analysis(src);
    i->pc = parse_block();
    expect(TOKEN_EOF);
    i->state = STATE_RUNNING;
}

void do_function_exit() {
    if (global_interpreter->returns.count == 0) {
        ERR("No more return\n");
    }

    return_call r = pop(&global_interpreter->returns);
    global_interpreter->symbol_count = r.stack_idx;
    global_interpreter->scope_depth--;

    value ret = {.type = VAL_NUM, .as.number = 0};
    if (global_interpreter->values_stack.count > 0) {
        ret = pop(&global_interpreter->values_stack);
    }

    if (!r.is_expr_call) {
        global_interpreter->pc = r.return_stmt;
    }
    global_interpreter->state = STATE_EXPR_EVALUATION;
    restore_previous_frame();
    append(&global_interpreter->values_stack, ret);
}

bool state_expr_evaluation() {
    if (step_expr()) {
        return true;
    }

    if ((int)global_interpreter->current_expr_plan_idx < global_interpreter->current_expr_plan.count) {
        return true;
    }

    if (global_interpreter->pending_return) {
        do_function_exit();
        return true;
    }

    // TODO: Replace with a EVAL_CONTINUATION_RESULT
    eval_continuation();

    if (global_interpreter->state == STATE_RUNNING) {
        return true;
    }

    if (global_interpreter->state == STATE_CONTINUE_EXPR_EVALUATION) {
        global_interpreter->state = STATE_EXPR_EVALUATION;
        return true;
    }

    if (global_interpreter->expr_frames.count > 0) {
        expr_frame top = global_interpreter->expr_frames.items[global_interpreter->expr_frames.count - 1];
        if (top.own_returns) {
            value ret = {.type = VAL_NUM, .as.number = 0};
            if (global_interpreter->values_stack.count > 0) {
                ret = pop(&global_interpreter->values_stack);
            }

            restore_previous_frame();
            append(&global_interpreter->values_stack, ret);
            return true;
        }
    }

    global_interpreter->state = STATE_RUNNING;
    global_interpreter->current_expr_plan.count = 0;
    global_interpreter->current_expr_plan_idx = 0;

    return true;
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

    if (should_go_to_sleep) {
        should_go_to_sleep = false;
        i->state = STATE_SLEEPING;
    }

    if (i->state == STATE_SLEEPING) {
        if (i->time_elapsed >= i->wakeup_time) {
            i->state = STATE_RUNNING;
        } else {
            return true;
        }
    } else if (i->state == STATE_EXPR_EVALUATION) {
        state_expr_evaluation();
        return true;
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

            if (function->type != SYMBOL_FUNCTION_NATIVE && function->type != SYMBOL_FUNCTION) {
                ERR("%s is not a function", call.function);
            }

            return_call r = {
                .return_stmt = s->next, .stack_idx = global_interpreter->symbol_count, .is_expr_call = false};
            append(&global_interpreter->returns, r);
            expr_save_frame_new((continuation){CONT_DISCARD, .as.stmt_discard.next = s->next});
            build_stmt_funcall_plan(&call);
            global_interpreter->state = STATE_EXPR_EVALUATION;
            push_nop();

            if (function->type == SYMBOL_FUNCTION_NATIVE) {
                if ((size_t)s->as.stmt_funcall.count != function->as.native_func.arg_count &&
                    function->as.native_func.variadic_arg_count == false) {
                    ERR("Function %s expected %d arguments but recieved %d", function->name,
                        function->as.funcdecl.arg_count, s->as.stmt_funcall.count);
                }
            }
            break;
        }
        case STMT_IF: {
            continuation c = {.type = CONT_IF, .as.stmt_if = {.if_branch = s->next, .else_branch = s->jmp}};
            expr_save_frame_new(c);
            build_expr_plan(&s->as.stmt_if.condition);
            push_nop();
            global_interpreter->state = STATE_EXPR_EVALUATION;
            break;
        }
        case STMT_FOR: {
            continuation min = {.type = CONT_LOOP_MIN, .as.cont_stmt_for = {.entry = &s->as.stmt_for, .end = s->jmp}};
            expr_save_frame_new(min);
            build_expr_plan(&s->as.stmt_for.min);

            continuation max = {.type = CONT_LOOP_MAX, .as.cont_stmt_for = {.entry = &s->as.stmt_for, .end = s->jmp}};
            expr_save_frame_new(max);
            build_expr_plan(&s->as.stmt_for.max);

            global_interpreter->state = STATE_EXPR_EVALUATION;
            break;
        }
        case STMT_FOREND: {
            const char *variable = s->as.stmt_for_end.variable;
            int min = s->as.stmt_for_end.min;
            int max = s->as.stmt_for_end.max;

            if (max == min) {
                i->pc = s->next;
            }

            symbol *var = get_symbol(variable);

            if (var == NULL) {
                s->as.stmt_for_end.stack_saved = global_interpreter->symbol_count;
                var = get_symbol_id(create_symbol(variable, SYMBOL_VARIABLE_INT));
                var->as.integer = min;
                var->scope_depth = global_interpreter->scope_depth;
            } else {
                var->as.integer++;
            }

            if (var->as.integer == max) {
                i->pc = s->next;
                global_interpreter->symbol_count = s->as.stmt_for_end.stack_saved;
            } else {
                i->pc = s->jmp;
            }
            break;
        }
        case STMT_NOP:
            i->pc = s->next;
            break;
        case STMT_VARIABLE: {
            const char *name = s->as.stmt_variable.variable;
            continuation c = {.type = CONT_ASSIGN, .as.stmt_assign = {.variable = name, .next = s->next}};
            expr_save_frame_new(c);
            build_expr_plan(&s->as.stmt_variable.expr);
            push_nop();
            global_interpreter->state = STATE_EXPR_EVALUATION;
            break;
        }
        case STMT_WHILE: {
            continuation should_loop = {.type = CONT_WHILE,
                                        .as.cont_stmt_while = {.entry = s->next, .out = s->jmp->next}};
            expr_save_frame_new(should_loop);
            build_expr_plan(&s->as.stmt_while.condition);
            global_interpreter->state = STATE_EXPR_EVALUATION;
            break;
        }
        case STMT_WHILEEND: {
            global_interpreter->pc = s->jmp;
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
        case STMT_FUNCTION_EXIT: {
            do_function_exit();
            break;
        }
        case STMT_RETURN: {
            expr_save_frame_new((continuation){.type = CONT_NONE});
            build_expr_plan(&s->as.stmt_return.expr);
            push_nop();
            global_interpreter->state = STATE_EXPR_EVALUATION;
            global_interpreter->pending_return = true;
            break;
        }
        case STMT_EXPR: {
            expr_save_frame_new((continuation){CONT_DISCARD, .as.stmt_discard.next = s->next});
            build_expr_plan(&s->as.stmt_expr.expr);
            global_interpreter->state = STATE_EXPR_EVALUATION;
            break;
        }
        default:
            ERR("Unknown statement");
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

int basic_pop_value_num() {
    value v = pop(&global_interpreter->values_stack);
    if (v.type != VAL_NUM) {
        ERR("Expected numeric value on top of stack");
    }
    return v.as.number;
}

const char *basic_pop_value_string() {
    value v = pop(&global_interpreter->values_stack);
    if (v.type != VAL_STRING) {
        ERR("Expected string value on top of stack");
    }
    return v.as.string;
}

int value_as_string(value v, char *out) {
    static char result[64] = {0};
    switch (v.type) {
        case VAL_NUM:
            int_to_str(v.as.number, result);
            if (out != NULL) {
                strcpy(out, result);
            }
            return strlen(result);
        case VAL_STRING:
            if (out != NULL) {
                strncpy(out, v.as.string, 64);
            }
            return strlen(v.as.string);
        default:
            ERR("Unknown value type %d", v.type);
    }
}

void basic_push_int(int result) {
    value v = {.type = VAL_NUM, .as.number = result};
    append(&global_interpreter->values_stack, v);
}

void basic_push_string(const char *s) {
    value v = {.type = VAL_STRING, .as.string = s};
    append(&global_interpreter->values_stack, v);
}

void basic_sleep(float seconds) {
    if (seconds < 0) {
        seconds = 0;
    }
    global_interpreter->wakeup_time = global_interpreter->time_elapsed + seconds;
    should_go_to_sleep = true;
}

#ifdef BASIC_TEST
const char *read_all_stdin(void) {
    size_t capacity = 8192;
    size_t size = 0;

    char *buffer = malloc(capacity + 1);
    if (!buffer)
        return NULL;

    while (1) {
        if (size == capacity) {
            capacity *= 2;
            char *tmp = realloc(buffer, capacity + 1);
            if (!tmp) {
                free(buffer);
                return NULL;
            }
            buffer = tmp;
        }

        size_t n = fread(buffer + size, 1, capacity - size, stdin);
        size += n;

        if (n == 0) {
            if (feof(stdin))
                break;
            if (ferror(stdin)) {
                free(buffer);
                return NULL;
            }
        }
    }

    buffer[size] = '\0';
    return buffer;
}

int main(int argc, const char **argv) {
    const char default_content[] = {
#embed "../assets/machines_impl/machine1/files/x"
    };
    basic_interpreter i = {.print_fn = default_print, .append_print_fn = default_print};

    if (argc == 2 && argv[1][0] == '-') {
        const char *content = read_all_stdin();
        init_interpreter(&i, content);
    } else {
        init_interpreter(&i, default_content);
    }

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
