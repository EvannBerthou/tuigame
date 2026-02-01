/*
 * TODO:
 *   - Table like in Lua
 *      - Array should just be a table with int key
 *   - Import another file
 *   - More math functions
 *   - Replace int with fixed sized uint16_t for values
 *   - Allow fixed point floating numbers
 *   - Use arena everywhere !!!
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
#include "basic_internals.h"

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
            __asm__("int3"); \
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

uint16_t read_word();
void print_program_bytecode() {
    printf("\n==== Program Bytecode ====\n");
    printf("IP = %zu (%s)\n", global_interpreter->ip, global_interpreter->current_function->name);
    size_t prev_ip = global_interpreter->ip;
    for (size_t f = 0; f < global_interpreter->bytecode.count; f++) {
        printf("\n== %s ==\n", global_interpreter->bytecode.items[f].name);
        function_code *function = &global_interpreter->bytecode.items[f];
        while (global_interpreter->ip < function->body.count) {
            size_t i = global_interpreter->ip++;
            opcode_type op = function->body.items[i];
            if (global_interpreter->current_function == function && prev_ip == i) {
                printf("-->");
            }
            printf("%04zu ", i);
            switch (op) {
                case OPCODE_VARIABLE:
                    printf("OPCODE_VARIABLE");
                    break;
                case OPCODE_CONSTANT_STRING:
                    printf("OPCODE_CONSTANT_STRING");
                    printf("\t\t%s", global_interpreter->values.items[read_word()].as.string);
                    i += 2;
                    break;
                case OPCODE_CONSTANT_NUMBER:
                    printf("OPCODE_CONSTANT_NUMBER");
                    printf("\t\t%d", read_word());
                    i += 2;
                    break;
                case OPCODE_EQEQ:
                    printf("OPCODE_EQEQ");
                    break;
                case OPCODE_NEQ:
                    printf("OPCODE_NEQ");
                    break;
                case OPCODE_LT:
                    printf("OPCODE_LT");
                    break;
                case OPCODE_LTE:
                    printf("OPCODE_LTE");
                    break;
                case OPCODE_GT:
                    printf("OPCODE_GT");
                    break;
                case OPCODE_GTE:
                    printf("OPCODE_GTE");
                    break;
                case OPCODE_ADD:
                    printf("OPCODE_ADD");
                    break;
                case OPCODE_SUB:
                    printf("OPCODE_SUB");
                    break;
                case OPCODE_MULT:
                    printf("OPCODE_MULT");
                    break;
                case OPCODE_DIV:
                    printf("OPCODE_DIV");
                    break;
                case OPCODE_NEGATE:
                    printf("OPCODE_NEGATE");
                    break;
                case OPCODE_ASSIGN:
                    printf("OPCODE_ASSIGN");
                    break;
                case OPCODE_FUNCALL:
                    printf("OPCODE_FUNCALL");
                    printf("\t\t\t%d", read_word());
                    i += 2;
                    break;
                case OPCODE_JUMP_IF_FALSE: {
                    printf("OPCODE_JUMP_IF_FALSE");
                    uint16_t offset = read_word();
                    printf("\t\t%05d", (int16_t)(i + offset + 3));
                    i += 2;
                    break;
                }
                case OPCODE_JUMP: {
                    printf("OPCODE_JUMP");
                    uint16_t offset = read_word();
                    printf("\t\t\t%05d", (int16_t)(i + offset + 3));
                    i += 2;
                    break;
                }
                case OPCODE_DISCARD:
                    printf("OPCODE_DISCARD");
                    break;
                case OPCODE_EOF:
                    printf("OPCODE_EOF");
                    break;
                case OPCODE_RETURN:
                    printf("OPCODE_RETURN");
                    break;
            }
            printf("\n");
        }
    }
    global_interpreter->ip = prev_ip;
}

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

token next(const char *input) {
    while (isspace(*input))
        input++;
    if (*input == '#') {
        while (*input && *input != '\n') {
            input++;
        }
        if (*input) {
            input++;
        }
    }
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
    } else if (isalpha(*input) || *input == '_') {
        result.type = TOKEN_IDENTIFIER;
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

size_t emit_opcode(opcode_type type) {
    size_t prev = global_interpreter->current_function->body.count;
    append(&global_interpreter->current_function->body, type);
    return prev;
}

size_t emit_word(uint16_t byte) {
    size_t prev = global_interpreter->current_function->body.count;
    append(&global_interpreter->current_function->body, byte & 0xFF);
    append(&global_interpreter->current_function->body, (byte >> 8) & 0xFF);
    return prev;
}

uint16_t read_word() {
    uint16_t result = 0;
    result += global_interpreter->current_function->body.items[global_interpreter->ip++] & 0xFF;
    result += (global_interpreter->current_function->body.items[global_interpreter->ip++] & 0xFF) << 8;
    return result;
}

int emit_value(value v) {
    append(&global_interpreter->values, v);
    return global_interpreter->values.count - 1;
}

void emit_constant_number(uint16_t num) {
    emit_opcode(OPCODE_CONSTANT_NUMBER);
    emit_word(num);
}

void emit_constant_string(const char *str) {
    emit_opcode(OPCODE_CONSTANT_STRING);
    int index = emit_value((value){.type = VAL_STRING, .as.string = str});
    emit_word(index);
}

void emit_variable_value(const char *var) {
    emit_constant_string(var);
    emit_opcode(OPCODE_VARIABLE);
}

void compile_expr();
void compile_block();

void compile_identifier() {
    token *tok = parser_next();
    if (peek_type(TOKEN_LPAREN)) {
        expect(TOKEN_LPAREN);
        size_t arg_count = 0;
        while (!peek_type(TOKEN_RPAREN)) {
            compile_expr();
            arg_count++;
        }
        expect(TOKEN_RPAREN);
        emit_constant_string(tok_to_str(tok));
        emit_opcode(OPCODE_FUNCALL);
        emit_word(arg_count);
    } else {
        emit_variable_value(tok_to_str(tok));
    }
}

void compile_primary() {
    if (peek_kw(KW_TRUE)) {
        parser_next();
        emit_constant_number(1);
    } else if (peek_kw(KW_FALSE)) {
        parser_next();
        emit_constant_number(0);
    } else if (peek_type(TOKEN_STRING)) {
        token *tok = parser_next();
        emit_constant_string(tok_to_str(tok));
    } else if (peek_type(TOKEN_IDENTIFIER)) {
        compile_identifier();
    } else if (peek_type(TOKEN_NUMBER)) {
        token *number = expect(TOKEN_NUMBER);
        emit_constant_number(tok_to_num(number));
    } else if (peek_type(TOKEN_LPAREN)) {
        parser_next();
        compile_expr();
        expect(TOKEN_RPAREN);
    } else {
        if (!peek_type(TOKEN_RPAREN)) {
            ERR("Unexpected %s in expr compilation\n", token_string[parser_peek()->type]);
        }
    }
}

void compile_unary() {
    if (peek_type(TOKEN_MINUS)) {
        expect(TOKEN_MINUS);
        compile_unary();
        emit_opcode(OPCODE_NEGATE);
    } else {
        compile_primary();
    }
}

void compile_mult() {
    compile_unary();
    while (peek_type(TOKEN_STAR) || peek_type(TOKEN_SLASH)) {
        token *op = parser_next();
        compile_unary();
        if (op->type == TOKEN_STAR) {
            emit_opcode(OPCODE_MULT);
        } else if (op->type == TOKEN_SLASH) {
            emit_opcode(OPCODE_DIV);
        }
    }
}

void compile_add() {
    compile_mult();
    while (peek_type(TOKEN_PLUS) || peek_type(TOKEN_MINUS)) {
        token *op = parser_next();
        compile_mult();
        if (op->type == TOKEN_PLUS) {
            emit_opcode(OPCODE_ADD);
        } else if (op->type == TOKEN_MINUS) {
            emit_opcode(OPCODE_SUB);
        }
    }
}

void compile_comparaisons() {
    compile_add();
    while (peek_type(TOKEN_EQEQ) || peek_type(TOKEN_NEQ) || peek_type(TOKEN_LT) || peek_type(TOKEN_LTE) ||
           peek_type(TOKEN_GT) || peek_type(TOKEN_GTE)) {
        token *tok = parser_next();
        compile_add();
        if (tok->type == TOKEN_EQEQ) {
            emit_opcode(OPCODE_EQEQ);
        } else if (tok->type == TOKEN_NEQ) {
            emit_opcode(OPCODE_NEQ);
        } else if (tok->type == TOKEN_LT) {
            emit_opcode(OPCODE_LT);
        } else if (tok->type == TOKEN_LTE) {
            emit_opcode(OPCODE_LTE);
        } else if (tok->type == TOKEN_GT) {
            emit_opcode(OPCODE_GT);
        } else if (tok->type == TOKEN_GTE) {
            emit_opcode(OPCODE_GTE);
        }
    }
}

void emit_and() {
}

void compile_and() {
    compile_comparaisons();

    while (peek_type(TOKEN_AND)) {
        parser_next();

        emit_opcode(OPCODE_JUMP_IF_FALSE);
        size_t jmp = emit_word(0);
        compile_and();
        uint16_t and_jmp_index = global_interpreter->current_function->body.count - jmp - 2;
        global_interpreter->current_function->body.items[jmp] = and_jmp_index & 0xFF;
        global_interpreter->current_function->body.items[jmp + 1] = (and_jmp_index >> 8) & 0xFF;
    }
}

void compile_or() {
    compile_and();

    while (peek_type(TOKEN_OR)) {
        parser_next();

        // If false
        emit_opcode(OPCODE_JUMP_IF_FALSE);
        size_t else_jmp = emit_word(0);

        // If true
        emit_opcode(OPCODE_CONSTANT_NUMBER);
        emit_word(1);
        emit_opcode(OPCODE_JUMP);
        size_t end_jmp = emit_word(0);

        uint16_t else_jmp_index = global_interpreter->current_function->body.count - else_jmp - 2;
        global_interpreter->current_function->body.items[else_jmp] = else_jmp_index & 0xFF;
        global_interpreter->current_function->body.items[else_jmp + 1] = (else_jmp_index >> 8) & 0xFF;

        compile_or();

        uint16_t end_jmp_index = global_interpreter->current_function->body.count - end_jmp - 2;
        global_interpreter->current_function->body.items[end_jmp] = end_jmp_index & 0xFF;
        global_interpreter->current_function->body.items[end_jmp + 1] = (end_jmp_index >> 8) & 0xFF;
    }
}

void compile_expr() {
    compile_or();
}

bool inside_function_declaration = false;
const char *last_function = NULL;

void compile_statement() {
    if (peek_type(TOKEN_IDENTIFIER)) {
        token *id = expect(TOKEN_IDENTIFIER);
        if (peek_type(TOKEN_EQUAL)) {
            parser_next();
            compile_expr();
            expect(TOKEN_SEMICOLON);
            emit_constant_string(tok_to_str(id));
            emit_opcode(OPCODE_ASSIGN);
        } else if (peek_type(TOKEN_LPAREN)) {
            expect(TOKEN_LPAREN);
            size_t arg_count = 0;
            while (!peek_type(TOKEN_RPAREN)) {
                compile_expr();
                arg_count++;
            }
            expect(TOKEN_RPAREN);
            expect(TOKEN_SEMICOLON);
            emit_constant_string(tok_to_str(id));
            emit_opcode(OPCODE_FUNCALL);
            emit_word(arg_count);
            emit_opcode(OPCODE_DISCARD);
        } else {
            ERR("Unknown identifier %s", tok_to_str(id));
        }
    } else if (peek_kw(KW_RETURN)) {
        parser_next();
        compile_expr();
        emit_opcode(OPCODE_RETURN);
        expect(TOKEN_SEMICOLON);
    } else if (peek_kw(KW_IF)) {
        // TODO: Support ELSE IF
        parser_next();
        compile_expr();
        expect(TOKEN_SEMICOLON);

        emit_opcode(OPCODE_JUMP_IF_FALSE);
        emit_word(0);

        size_t saved = global_interpreter->current_function->body.count;
        compile_block();
        size_t after = global_interpreter->current_function->body.count;

        if (peek_kw(KW_ELSE)) {
            parser_next();
            emit_opcode(OPCODE_JUMP);
            emit_word(0);
            size_t else_saved = global_interpreter->current_function->body.count;
            compile_block();
            size_t else_after = global_interpreter->current_function->body.count;

            uint16_t after_index = after - saved + 3;
            global_interpreter->current_function->body.items[saved - 2] = after_index & 0xFF;
            global_interpreter->current_function->body.items[saved - 1] = (after_index >> 8) & 0xFF;

            uint16_t else_saved_index = else_after - else_saved;
            global_interpreter->current_function->body.items[else_saved - 2] = else_saved_index & 0xFF;
            global_interpreter->current_function->body.items[else_saved - 1] = (else_saved_index >> 8) & 0xFF;
        } else {
            uint16_t after_index = after - saved;
            global_interpreter->current_function->body.items[saved - 2] = after_index & 0xFF;
            global_interpreter->current_function->body.items[saved - 1] = (after_index >> 8) & 0xFF;
        }

        expect_kw(KW_END);
    } else if (peek_kw(KW_FOR)) {
        parser_next();
        token *tok = expect(TOKEN_IDENTIFIER);
        const char *variable_name = tok_to_str(tok);
        expect_kw(KW_IN);

        compile_expr();
        emit_constant_string(variable_name);
        emit_opcode(OPCODE_ASSIGN);
        expect(TOKEN_DOT);
        expect(TOKEN_DOT);

        size_t loop_start = global_interpreter->current_function->body.count;
        compile_expr();
        expect(TOKEN_SEMICOLON);
        emit_variable_value(variable_name);
        emit_opcode(OPCODE_GT);
        emit_opcode(OPCODE_JUMP_IF_FALSE);
        emit_word(0);
        size_t loop_jump = global_interpreter->current_function->body.count;

        compile_block();
        expect_kw(KW_END);

        emit_constant_number(1);
        emit_variable_value(variable_name);
        emit_opcode(OPCODE_ADD);
        emit_constant_string(variable_name);
        emit_opcode(OPCODE_ASSIGN);

        size_t end = global_interpreter->current_function->body.count;

        emit_opcode(OPCODE_JUMP);
        emit_word(loop_start - end - 3);

        uint16_t jmp_index = end - loop_jump + 3;
        global_interpreter->current_function->body.items[loop_jump - 2] = jmp_index & 0xFF;
        global_interpreter->current_function->body.items[loop_jump - 1] = (jmp_index >> 8) & 0xFF;
    } else if (peek_kw(KW_WHILE)) {
        parser_next();

        size_t loop_start = global_interpreter->current_function->body.count;
        compile_expr();
        expect(TOKEN_SEMICOLON);
        emit_opcode(OPCODE_JUMP_IF_FALSE);
        emit_word(0);
        size_t loop_jump = global_interpreter->current_function->body.count;

        compile_block();
        expect_kw(KW_END);

        size_t end = global_interpreter->current_function->body.count;

        emit_opcode(OPCODE_JUMP);
        emit_word(loop_start - end - 3);

        uint16_t jmp_index = end - loop_jump + 3;
        global_interpreter->current_function->body.items[loop_jump - 2] = jmp_index & 0xFF;
        global_interpreter->current_function->body.items[loop_jump - 1] = (jmp_index >> 8) & 0xFF;
    } else if (peek_kw(KW_FUNC)) {
        parser_next();
        const char *function_name = tok_to_str(expect(TOKEN_IDENTIFIER));
        if (inside_function_declaration) {
            ERR("Nested function declaration are not allowed.\nTrying to define %s inside %s", function_name,
                last_function);
        }
        function_code new_func = {.name = function_name};
        expect(TOKEN_LPAREN);
        while (!peek_type(TOKEN_RPAREN) && !peek_type(TOKEN_EOF)) {
            token *arg = expect(TOKEN_IDENTIFIER);
            append(&new_func.args, tok_to_str(arg));
        }
        expect(TOKEN_RPAREN);
        expect(TOKEN_SEMICOLON);
        append(&global_interpreter->bytecode, new_func);
        global_interpreter->current_function =
            &global_interpreter->bytecode.items[global_interpreter->bytecode.count - 1];
        {
            inside_function_declaration = true;
            last_function = function_name;
            compile_block();
            inside_function_declaration = false;
            last_function = NULL;
            // TODO: We should add implicit return only if there is no explicit return at the end
            if (global_interpreter->current_function->body.count == 0 ||
                global_interpreter->current_function->body
                        .items[global_interpreter->current_function->body.count - 1] != OPCODE_RETURN) {
                emit_constant_number(0);
                emit_opcode(OPCODE_RETURN);
            }

            symbol *s = get_symbol_id(create_symbol(function_name, SYMBOL_FUNCTION));
            s->as.funcdecl.body = global_interpreter->current_function;
            s->as.funcdecl.args = global_interpreter->current_function->args.items;
            s->as.funcdecl.arg_count = global_interpreter->current_function->args.count;
        }

        global_interpreter->current_function = &global_interpreter->bytecode.items[0];
        expect_kw(KW_END);
    } else {
        compile_expr();
        expect(TOKEN_SEMICOLON);
        emit_opcode(OPCODE_DISCARD);
    }
}

void compile_block() {
    while (!peek_type(TOKEN_EOF) && !peek_kw(KW_END) && !peek_kw(KW_ELSE)) {
        compile_statement();
    }
}

void compile_program() {
    compile_block();
}

symbol *get_symbol(const char *name) {
    for (int i = global_interpreter->symbols_table_count - 1; i >= 0; i--) {
        symbol *s = &global_interpreter->symbols_table[i];
        if (s && s->type != SYMBOL_NONE && strcmp(s->name, name) == 0) {
            if (s->depth == 0 || s->depth == global_interpreter->depth) {
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
    if (global_interpreter->symbols_table_count == MAX_SYMBOL_COUNT) {
        ERR("No more space to allocate more symbols");
    }
    symbol *s = &global_interpreter->symbols_table[global_interpreter->symbols_table_count];
    s->name = name;
    s->type = type;
    s->depth = global_interpreter->depth;
    return global_interpreter->symbols_table_count++;
}

size_t create_symbol_from_value(const char *name, value v) {
    if (v.type == VAL_NUM) {
        size_t s = create_symbol(name, SYMBOL_VARIABLE_INT);
        get_symbol_id(s)->as.integer = v.as.number;
        return s;
    } else if (v.type == VAL_STRING) {
        size_t s = create_symbol(name, SYMBOL_VARIABLE_STRING);
        get_symbol_id(s)->as.string = v.as.string;
        return s;
    }
    ERR("Unknown value type %d", v.type);
}

void prepare_funcall_args(symbol *function) {
    for (size_t i = 0; i < function->as.funcdecl.arg_count; i++) {
        create_symbol(function->as.funcdecl.args[i], SYMBOL_VARIABLE_INT);
    }
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
    for (size_t i = global_interpreter->sp; i < global_interpreter->stack.count; i++) {
        value v = global_interpreter->stack.items[i];
        print_val(&v);
        interpreter_log(" ");
    }
    global_interpreter->stack.count = global_interpreter->sp;
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

bool is_true(value v) {
    if (v.type == VAL_NUM) {
        return v.as.number != 0;
    }
    if (v.type == VAL_STRING) {
        return strlen(v.as.string) != 0;
    }
    ERR("Unknown value type");
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

void default_print(const char *text) {
    printf("%s", text);
}

long long timeInMilliseconds(void) {
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (((long long)tv.tv_sec) * 1000) + (tv.tv_usec / 1000);
}

// Externals

bool interpreter_init(const char *src, void (*print_fn)(const char *), void (*append_fn)(const char *)) {
    interpreter_arena = arena_default();
    global_interpreter = arena_alloc(interpreter_arena, sizeof(*global_interpreter));
    memset(global_interpreter, 0, sizeof(*global_interpreter));
    global_interpreter->print_fn = print_fn == NULL ? default_print : print_fn;
    global_interpreter->append_print_fn = append_fn == NULL ? default_print : append_fn;
    parser_reader = 0;
    token_count = 0;
    // TODO: Should not exit on first error
    volatile int error_code = 0;
    if ((error_code = setjmp(err_jmp)) != 0) {
        if (error_code != -1) {
            interpreter_log("\nexit from error from line %d\n", error_line);
        }
        destroy_interpreter();
        return false;
    }
    register_std_lib();
    lexical_analysis(src);

    function_code main = {.name = "main"};
    append(&global_interpreter->bytecode, main);
    global_interpreter->current_function = &global_interpreter->bytecode.items[0];

    if (!peek_type(TOKEN_EOF)) {
        compile_program();
    }
    expect(TOKEN_EOF);
    emit_opcode(OPCODE_EOF);
    global_interpreter->state = STATE_RUNNING;
    return true;
}

void advance_interpreter_time(float time) {
    global_interpreter->time_elapsed += time;
}

bool step_bytecode() {
    opcode_type op = global_interpreter->current_function->body.items[global_interpreter->ip++];
    switch (op) {
        case OPCODE_CONSTANT_STRING: {
            uint16_t index = read_word();
            value value = global_interpreter->values.items[index];
            append(&global_interpreter->stack, value);
            return true;
        } break;
        case OPCODE_CONSTANT_NUMBER: {
            uint16_t v = read_word();
            value res = {.type = VAL_NUM, .as.number = v};
            append(&global_interpreter->stack, res);
            return true;
        } break;
        case OPCODE_EOF:
            return false;
        case OPCODE_ADD: {
            basic_push_int(basic_pop_value_num() + basic_pop_value_num());
            return true;
        }
        case OPCODE_MULT: {
            basic_push_int(basic_pop_value_num() * basic_pop_value_num());
            return true;
        }
        case OPCODE_SUB: {
            int b = basic_pop_value_num();
            int a = basic_pop_value_num();
            basic_push_int(a - b);
            return true;
        }
        case OPCODE_DIV: {
            int b = basic_pop_value_num();
            int a = basic_pop_value_num();
            basic_push_int(a / b);
            return true;
        }
        case OPCODE_EQEQ: {
            basic_push_int(basic_pop_value_num() == basic_pop_value_num());
            return true;
        }
        case OPCODE_NEQ: {
            basic_push_int(basic_pop_value_num() != basic_pop_value_num());
            return true;
        }
        case OPCODE_LT: {
            int b = basic_pop_value_num();
            int a = basic_pop_value_num();
            basic_push_int(a < b);
            return true;
        }
        case OPCODE_LTE: {
            int b = basic_pop_value_num();
            int a = basic_pop_value_num();
            basic_push_int(a <= b);
            return true;
        }
        case OPCODE_GT: {
            int b = basic_pop_value_num();
            int a = basic_pop_value_num();
            basic_push_int(a > b);
            return true;
        }
        case OPCODE_GTE: {
            int b = basic_pop_value_num();
            int a = basic_pop_value_num();
            basic_push_int(a >= b);
            return true;
        }
        case OPCODE_NEGATE: {
            basic_push_int(-basic_pop_value_num());
            return true;
        } break;
        case OPCODE_FUNCALL: {
            const char *function_name = basic_pop_value_string();
            symbol *function = get_symbol(function_name);
            if (function == NULL) {
                ERR("Unknown symbol %s", function_name);
            }
            if (function->type == SYMBOL_FUNCTION_NATIVE) {
                uint16_t funcall_arg_count = read_word();
                size_t expected = function->as.native_func.arg_count;
                if (funcall_arg_count != expected && function->as.native_func.variadic_arg_count == false) {
                    ERR("Function %s expected %zu args but recieved %zu", function_name, expected, funcall_arg_count);
                }
                function->as.native_func.function();
            } else if (function->type == SYMBOL_FUNCTION) {
                uint16_t funcall_arg_count = read_word();
                size_t expected = function->as.funcdecl.arg_count;
                if (funcall_arg_count != expected) {
                    ERR("Function %s expected %zu args but recieved %zu", function_name, expected, funcall_arg_count);
                }
                global_interpreter->depth++;
                size_t previous_symbol_count = global_interpreter->symbols_table_count;
                for (int i = expected - 1; i >= 0; i--) {
                    create_symbol_from_value(function->as.funcdecl.args[i], pop(&global_interpreter->stack));
                }
                return_frame frame = {global_interpreter->current_function, global_interpreter->ip,
                                      global_interpreter->sp, previous_symbol_count};
                append(&global_interpreter->return_stack, frame);
                global_interpreter->current_function = function->as.funcdecl.body;
                global_interpreter->ip = 0;
                global_interpreter->sp = global_interpreter->stack.count;
            } else {
                ERR("%s is not a function", function->name);
            }
            return true;
        } break;
        case OPCODE_ASSIGN: {
            const char *variable_name = basic_pop_value_string();
            value v = pop(&global_interpreter->stack);

            symbol *s = get_symbol(variable_name);
            if (s == NULL) {
                create_symbol_from_value(variable_name, v);
            } else {
                if (v.type == VAL_NUM) {
                    s->type = SYMBOL_VARIABLE_INT;
                    s->as.integer = v.as.number;
                } else if (v.type == VAL_STRING) {
                    s->type = SYMBOL_VARIABLE_STRING;
                    s->as.string = v.as.string;
                }
            }
            return true;
        } break;
        case OPCODE_VARIABLE:
            const char *variable_name = basic_pop_value_string();
            symbol *s = get_symbol(variable_name);
            if (!s) {
                ERR("Unknown variable %s", variable_name);
            }
            if (s->type == SYMBOL_VARIABLE_INT) {
                basic_push_int(s->as.integer);
            } else if (s->type == SYMBOL_VARIABLE_STRING) {
                basic_push_string(s->as.string);
            }
            return true;
            break;
        case OPCODE_JUMP_IF_FALSE: {
            value result = pop(&global_interpreter->stack);
            uint16_t offset = read_word();
            if (!is_true(result)) {
                global_interpreter->ip += (int16_t)offset;
            }
            opcode_type next = global_interpreter->current_function->body.items[global_interpreter->ip];
            if (next == OPCODE_JUMP_IF_FALSE) {
                basic_push_int(is_true(result));
            }
            return true;
        } break;
        case OPCODE_JUMP: {
            uint16_t offset = read_word();
            global_interpreter->ip += (int16_t)offset;
            return true;
        } break;
        case OPCODE_DISCARD: {
            (void)pop(&global_interpreter->stack);
            return true;
        } break;
        case OPCODE_RETURN: {
            return_frame frame = pop(&global_interpreter->return_stack);
            global_interpreter->current_function = frame.function;
            global_interpreter->ip = frame.ip;
            global_interpreter->sp = frame.sp;
            global_interpreter->symbols_table_count = frame.symbols_count;
            global_interpreter->depth--;
            return true;
        } break;
    }
    ERR("Unknown opcode of type %d", op);
    return false;
}

bool step_program() {
    if (global_interpreter->state == STATE_SLEEPING) {
        return true;
    }

    if (global_interpreter->ip >= global_interpreter->current_function->body.count) {
        ERR("Something went wrong with ip");
    }
    return step_bytecode();
}

void destroy_interpreter() {
    global_interpreter = NULL;
    arena_free(interpreter_arena);
    interpreter_arena = NULL;
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
    s->as.integer = value;
}

void register_variable_string(const char *name, const char *value) {
    symbol *s = get_symbol_id(create_symbol(name, SYMBOL_VARIABLE_STRING));
    s->as.string = value;
}

void basic_push_int(int result) {
    value v = {.type = VAL_NUM, .as.number = result};
    append(&global_interpreter->stack, v);
}

void basic_push_string(const char *s) {
    value v = {.type = VAL_STRING, .as.string = s};
    append(&global_interpreter->stack, v);
}

int basic_pop_value_num() {
    value v = pop(&global_interpreter->stack);
    if (v.type != VAL_NUM) {
        ERR("Expected numeric value on top of stack");
    }
    return v.as.number;
}

const char *basic_pop_value_string() {
    value v = pop(&global_interpreter->stack);
    if (v.type != VAL_STRING) {
        ERR("Expected string value on top of stack");
    }
    return v.as.string;
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
    if (argc == 2 && argv[1][0] == '-') {
        const char *content = read_all_stdin();
        if (!interpreter_init(content, NULL, NULL))
            return 1;
    } else {
        if (!interpreter_init(default_content, NULL, NULL))
            return 1;
    }

    // print_program_bytecode();
    long long last_time = timeInMilliseconds();
    while (true) {
        long long new_time = timeInMilliseconds();
        advance_interpreter_time((new_time - last_time) / 1000.f);
        if (!step_program())
            break;
        last_time = new_time;
    }
    return 0;
}
#endif
