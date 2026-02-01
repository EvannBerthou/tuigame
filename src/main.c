#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "arena.h"
#include "basic.h"
#include "bootseq.h"
#include "commands.h"
#include "raylib.h"
#define GLSL_VERSION 330

const float WIDTH = 1280;
const float HEIGHT = 720;

#define FB_SIZE_WIDTH 80
#define FB_SIZE_HEIGHT 60
#define FB_SIZE (FB_SIZE_WIDTH * FB_SIZE_HEIGHT)

#define MAX_LOG_COUNT 4096

typedef struct terminal terminal;

Shader terminal_shader = {0};
RenderTexture2D target = {0};
Font terminal_font = {0};

const int font_size = 28;
terminal *all_terminals = NULL;
terminal *active_term = NULL;
int terminal_count = 0;

#define MAIL_PROCESS_ANWSER_MAX_LENGTH 23

typedef struct {
    size_t selected_mail_idx;
    char anwser[MAIL_PROCESS_ANWSER_MAX_LENGTH];
    size_t anwser_ptr;
} mail_process;

#define NETSCAN_SCAN_TIME (1.f / 60.f)

typedef struct {
    const char *scanning_network_ip;
    int current_progress;
    float scantime_left;
    bool found[256];
} netscan_process;

typedef struct {
    int selected_index;
    int offset;
} help_process;

typedef struct {
    int fb[FB_SIZE];
} test_process;

typedef struct {
    const char *filename;
    int fb[2][FB_SIZE];
    int fb_idx;
} exec_process;

typedef struct {
    int selected_index;
    struct {
        terminal **items;
        int count;
        int capacity;
    } machines;
} machines_process;

#define MAX_INPUT_LENGTH 40
#define MAX_LINE_COUNT_PER_SCREEN 21

typedef struct {
    char **items;
    int count;
    int capacity;
} text_lines;

void free_text_lines(text_lines *lines) {
    if (lines == NULL) {
        return;
    }
    for (int i = 0; i < lines->count; i++) {
        free(lines->items[i]);
    }
    free(lines->items);
    lines->items = NULL;
    lines->count = 0;
    lines->capacity = 0;
}

text_lines text_split(char *content, char sep) {
    text_lines result = {0};

    if (content == NULL) {
        return result;
    }

    char *s = content;
    char *last = s;
    while (*s) {
        if (*s == sep) {
            *s = '\0';
            append(&result, strdup(last));
            s++;
            last = s;
        } else {
            s++;
        }
    }
    return result;
}

size_t get_lines_total_size(text_lines *lines) {
    size_t size = 0;
    for (int i = 0; i < lines->count; i++) {
        size += strlen(lines->items[i]);
    }
    return size;
}

typedef struct file_node file_node;

struct file_node {
    file_node *parent;
    const char *name;
    bool folder;
    text_lines lines;
    size_t content_size;
    struct {
        file_node **items;
        int capacity;
        int count;
    } children;
};

const char *node_get_content(file_node *node) {
    if (node->folder) {
        return NULL;
    }
    char *content = malloc(node->content_size + node->lines.count + 1);
    assert(content != NULL);
    content[0] = '\0';

    char *s = content;
    for (int i = 0; i < node->lines.count; i++) {
        s = strcat(s, node->lines.items[i]);
        s = strcat(s, " ");
    }
    return content;
}

void file_node_append_children(file_node *root, file_node *children) {
    if (root->folder == false) {
        return;
    }
    children->parent = root;
    append(&root->children, children);
}

file_node *file_new(const char *name, text_lines lines) {
    file_node *result = calloc(1, sizeof(*result));
    assert(result != NULL);
    result->name = strdup(name);
    result->lines = lines;
    result->content_size = get_lines_total_size(&result->lines);
    return result;
}

file_node *folder_new(const char *name) {
    file_node *result = calloc(1, sizeof(*result));
    assert(result != NULL);
    result->name = strdup(name);
    result->folder = true;
    return result;
}

file_node *find_node_in_dir(file_node *root, const char *filename) {
    if (root == NULL) {
        return NULL;
    }
    if (filename[0] == '/') {
        return NULL;
    }

    if (strcmp(filename, "..") == 0) {
        return root->parent;
    }

    if (strcmp(filename, ".") == 0) {
        return root;
    }

    for (int i = 0; i < root->children.count; i++) {
        if (strcmp(filename, root->children.items[i]->name) == 0) {
            return root->children.items[i];
        }
    }

    return NULL;
}

file_node *find_fs_root(file_node *node) {
    while (node->parent != node) {
        node = node->parent;
    }
    return node;
}

file_node *look_up_node(file_node *root, const char *path) {
    static char local_str[256] = {0};

    if (path[0] == '/') {
        root = find_fs_root(root);
        strncpy(local_str, path + 1, 256);
    } else {
        strncpy(local_str, path, 256);
    }

    int offset = 0;
    int folder_length = 0;
    do {
        folder_length = TextFindIndex(local_str + offset, "/");
        if (folder_length == -1) {
            break;
        }
        root = find_node_in_dir(root, TextFormat("%.*s", folder_length, local_str + offset));
        offset += folder_length + 1;
    } while (true);

    if (*(local_str + offset) != '\0') {
        root = find_node_in_dir(root, TextFormat("%.*s", folder_length, local_str + offset));
    }

    return root;
}

void file_node_append_folder_full_path(file_node *root, const char *path) {
    int idx = 0;
    do {
        int next_directory = TextFindIndex(path + idx, "/");
        if (next_directory == -1) {
            break;
        }
        idx += next_directory + 1;
    } while (true);
    file_node *parent = look_up_node(root, TextFormat("%.*s", idx, path));
    file_node_append_children(parent, folder_new(path + idx));
}

void file_node_append_file_full_path(file_node *root, const char *path, text_lines lines) {
    int idx = 0;
    do {
        int next_directory = TextFindIndex(path + idx, "/");
        if (next_directory == -1) {
            break;
        }
        idx += next_directory + 1;
    } while (true);
    file_node *parent = look_up_node(root, TextFormat("%.*s", idx, path));
    file_node_append_children(parent, file_new(path + idx, lines));
}

const char *get_file_full_path(file_node *node) {
    static char full_path[256] = {0};

    memset(full_path, 0, sizeof(full_path));
    file_node *path_nodes[16] = {0};
    int path_nodes_count = 0;
    while (node->parent != node) {
        path_nodes[path_nodes_count++] = node;
        node = node->parent;
    }
    if (path_nodes_count == 0) {
        strcat(full_path, "/");
        return full_path;
    }
    for (int i = path_nodes_count - 1; i >= 0; i--) {
        strcat(full_path, "/");
        strcat(full_path, path_nodes[i]->name);
    }

    return full_path;
}

file_node *get_parent_from_path(file_node *root, const char *path) {
    static char local_str[256] = {0};

    if (path[0] == '\0') {
        return root;
    }

    if (path[0] == '/') {
        root = find_fs_root(root);
        strncpy(local_str, path + 1, 256);
    } else {
        strncpy(local_str, path, 256);
    }

    int offset = 0;
    do {
        int folder_length = TextFindIndex(local_str + offset, "/");
        if (folder_length == -1) {
            break;
        }
        root = find_node_in_dir(root, TextFormat("%.*s", folder_length, local_str + offset));
        offset += folder_length + 1;
    } while (true);

    return root;
}

const char *get_path_filename(const char *path) {
    int next_slash_index = 0;
    while ((next_slash_index = TextFindIndex(path, "/")) != -1) {
        path += next_slash_index + 1;
    }
    return path;
}

typedef struct {
    file_node *root;
    file_node *pwd;
} filesystem;

typedef int (*process_init)(terminal *term, int argc, const char **argv);
typedef int (*process_update)(terminal *term);
typedef void (*process_render)(terminal *term);

struct terminal {
    const char *title;
    const char *hostname;
    const char *ip;

    const char *logs[MAX_LOG_COUNT];
    size_t log_head;
    size_t log_tail;
    size_t log_count;

    int scroll_offset;

    // TODO: Cursor mouvements
    char input[MAX_INPUT_LENGTH + 1];
    int input_cursor;

    struct {
        const char **items;
        int count;
        int capacity;
    } history;
    int history_ptr;

    process_update process_update;
    process_render process_render;
    void *args;
    bool render_not_ready;
    bool process_should_exit;

    filesystem fs;
    bool connected;
};

typedef struct {
    const char *name;
    const char *alias;
    process_init init;
    process_update update;
    process_render render;
} command;

// TODO: Command ideas:
//   iv -> Image viewer
//   rm/mv -> file manipulation

// TODO: Command improvements
//   ls -> Add date

#define COMMANDS         \
    XI(list, "ls")       \
    XI(cd, NULL)         \
    XI(echo, NULL)       \
    XI(print, NULL)      \
    XI(path, NULL)       \
    XI(pwd, NULL)        \
    XI(create, NULL)     \
    XI(connect, NULL)    \
    XIUR(machines, NULL) \
    XI(hostname, NULL)   \
    XIUR(mail, NULL)     \
    XI(shutdown, NULL)   \
    XI(clear, NULL)      \
    XI(ping, NULL)       \
    XIUR(edit, NULL)     \
    XIUR(help, NULL)     \
    XIUR(exec, NULL)     \
    XIU(netscan, "ns")

#define XI(n, a) int n##_init(terminal *term, int argc, const char **argv);
#define XIU(n, a)                                              \
    int n##_init(terminal *term, int argc, const char **argv); \
    int n##_update(terminal *term);
#define XIUR(n, a) XIU(n, a) void n##_render(terminal *term);
COMMANDS
#undef XI
#undef XIU
#undef XIUR

#define XI(n, a, ...) {.name = #n, .alias = a, .init = n##_init, __VA_ARGS__},
#define XIU(n, a, ...) XI(n, a, .update = n##_update, __VA_ARGS__)
#define XIUR(n, a) XIU(n, a, .render = n##_render)
command commands[] = {COMMANDS};
#undef XI
#undef XIU
#undef XIUR

typedef struct {
    const char *name;
    const char *text;
} command_help_pair;

#define X(n) {#n, n##_help},
#define XI(n, a) X(n)
#define XIU(n, a) X(n)
#define XIUR(n, a) X(n)
command_help_pair commands_help[] = {COMMANDS};
#undef X
#undef XI
#undef XIU
#undef XIUR

const size_t command_count = sizeof(commands) / sizeof(*commands);

const char *get_command_help(const char *cmd) {
    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(cmd, commands_help[i].name) == 0) {
            return (const char *)commands_help[i].text;
        }
    }
    return "No help page found";
}

int get_command_id(const char *cmd) {
    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(cmd, commands[i].name) == 0) {
            return i;
        }
        if (commands[i].alias != NULL && strcmp(cmd, commands[i].alias) == 0) {
            return i;
        }
    }
    return -1;
}

#define NETWORK_MAX_NODE_COUNT 8

// TODO; Maybe use dynarray ?
typedef struct {
    struct {
        const char *ip;
        terminal *term;
    } nodes[NETWORK_MAX_NODE_COUNT];
    int node_count;
} network;

#define MAX_NETWORK_COUNT 10
network networks[MAX_NETWORK_COUNT] = {0};

bool is_ip_format(const char *s) {
    if (s == NULL) {
        return false;
    }
    int dots = 0;
    int num = 0;
    int digits = 0;
    int leading_zero = 0;

    while (*s) {
        if (isdigit(*s)) {
            if (digits == 0 && *s == '0') {
                leading_zero = 1;
            } else if (leading_zero) {
                return false;
            }
            num = num * 10 + (*s - '0');
            if (num > 255) {
                return false;
            }
            digits++;
            if (digits > 3) {
                return false;
            }
        } else if (*s == '.') {
            if (digits == 0) {
                return false;
            }
            dots++;
            if (dots > 3) {
                return false;
            }
            num = 0;
            digits = 0;
            leading_zero = 0;
        } else {
            return false;
        }
        s++;
    }

    return !(dots != 3 || digits == 0);
}

const char *machine_ip_to_network(const char *ip) {
    int ip_len = strlen(ip) + 1;
    char *result = malloc(ip_len);
    assert(result != NULL);
    snprintf(result, ip_len, "%.*s0", (int)(strrchr(ip, '.') - ip + 1), ip);
    return result;
}

const char *network_ip_get_machine(const char *network, int machine) {
    static char ip[17] = {0};
    if (machine < 0 || machine > 255) {
        return NULL;
    }
    snprintf(ip, sizeof(ip), "%.*s%d", (int)(strrchr(network, '.') - network + 1), network, machine);
    return ip;
}

bool network_append_machine(network *net, terminal *term) {
    if (is_ip_format(term->ip) == false) {
        return false;
    }

    if (net->node_count == 8) {
        return false;
    }
    for (int i = 0; i < net->node_count; i++) {
        if (strcmp(net->nodes[i].ip, term->ip) == 0) {
            return false;
        }
    }
    net->nodes[net->node_count].ip = term->ip;
    net->nodes[net->node_count].term = term;
    net->node_count++;

    return true;
}

terminal *network_get_terminal(network *net, const char *ip) {
    for (int i = 0; i < net->node_count; i++) {
        if (strcmp(net->nodes[i].ip, ip) == 0) {
            return net->nodes[i].term;
        }
    }
    return NULL;
}

bool ip_is_reachable(const terminal *from, const char *ip) {
    for (int i = 0; i < MAX_NETWORK_COUNT; i++) {
        const terminal *me = network_get_terminal(&networks[i], from->ip);
        const terminal *to = network_get_terminal(&networks[i], ip);
        if (me != NULL && to != NULL) {
            return true;
        }
    }
    return false;
}

// (x,y) in screen coordintates
Vector2 S(int x, int y) {
    return (Vector2){x + 60, y + 80};
}

Vector2 V(int x, int y) {
    return (Vector2){x, y};
}

void DrawTerminalLine(const char *text, int line) {
    DrawTextEx(terminal_font, text, S(0, font_size * line), font_size, 1, WHITE);
}

void DrawHoveredTerminalLine(const char *text, int line) {
    DrawRectangleV(S(0, -5 + font_size * line), V(WIDTH, font_size + 5), WHITE);
    DrawTextEx(terminal_font, text, S(0, font_size * line), font_size, 1, BLACK);
}

void terminal_append_log(terminal *term, const char *text) {
    if (term->log_count == MAX_LOG_COUNT) {
        size_t oldest = (term->log_head + MAX_LOG_COUNT - term->log_count) % MAX_LOG_COUNT;
        free((void *)term->logs[oldest]);
    } else {
        term->log_count++;
    }

    term->logs[term->log_head] = strdup(text);
    term->log_head = (term->log_head + 1) % MAX_LOG_COUNT;

    if (term->log_count >= MAX_LINE_COUNT_PER_SCREEN) {
        term->scroll_offset = term->log_count - MAX_LINE_COUNT_PER_SCREEN;
    } else {
        term->scroll_offset = 0;
    }
}

void terminal_log_append_text(terminal *term, const char *text) {
    size_t last = (term->log_head == 0 ? MAX_LOG_COUNT : term->log_head) - 1;
    const char *last_line = term->logs[last];
    int current_len = strlen(last_line);
    int new_text_len = strlen(text);
    int new_len = current_len + new_text_len + 1;
    char *new_line = malloc(new_len);
    assert(new_line != NULL);
    memset(new_line, 0, new_len);
    strncpy(new_line, last_line, current_len);
    strncat(new_line, text, new_text_len);
    free((void *)last_line);
    term->logs[last] = new_line;
}

void terminal_replace_last_line(terminal *term, const char *text) {
    if (term->log_count == 0) {
        terminal_append_log(term, text);
        return;
    }

    size_t last = (term->log_head == 0 ? MAX_LOG_COUNT : term->log_head) - 1;
    char *last_line = (char *)term->logs[last];
    size_t last_line_len = strlen(last_line) + 1;
    size_t new_len = strlen(text) + 1;
    if (last_line_len < new_len) {
        term->logs[last] = realloc(last_line, new_len);
    }
    strncpy(last_line, text, last_line_len);
}

void terminal_basic_print(const char *text) {
    // printf("%s", text);
    if (*text == '\n') {
        terminal_append_log(active_term, "");
    } else {
        terminal_append_log(active_term, text);
    }
}

void terminal_append_print(const char *text) {
    // printf("[Basic]: %s", text);
    terminal_log_append_text(active_term, text);
}

void terminal_append_input(terminal *term) {
    terminal_append_log(term, TextFormat("$%.*s", term->input_cursor, term->input));
    append(&term->history, strdup(TextFormat("%.*s", term->input_cursor, term->input)));
    term->input_cursor = 0;
}

void terminal_render(const terminal *term) {
    size_t available = term->log_count;

    size_t start = term->scroll_offset;
    size_t end = fmin(start + MAX_LINE_COUNT_PER_SCREEN, available);

    for (size_t i = start; i < end; i++) {
        size_t idx = (term->log_head + MAX_LOG_COUNT - term->log_count + i) % MAX_LOG_COUNT;
        DrawTerminalLine(term->logs[idx], i - term->scroll_offset);
    }
}

void terminal_render_prompt(terminal *term) {
    int last_line_index = fmin(MAX_LINE_COUNT_PER_SCREEN, term->log_count);
    const char *prompt_text = TextFormat("$%.*s", term->input_cursor, term->input);
    DrawTerminalLine(prompt_text, last_line_index);
    // TODO: Blink qu'après 1 seconde d'inactivité
    if ((int)GetTime() % 2 == 0) {
        int x = MeasureTextEx(terminal_font, prompt_text, font_size, 1).x + 8;
        int y = font_size * last_line_index + 4;
        DrawRectangleV(S(x, y), V(10, font_size - 8), WHITE);
    }
}

bool first_frame_enter = false;

bool key_pressed(int key) {
    if (key == KEY_ENTER && first_frame_enter)
        return false;
    return IsKeyPressed(key) || IsKeyPressedRepeat(key);
}

bool key_pressed_control(int key) {
    return IsKeyDown(KEY_LEFT_CONTROL) && key_pressed(key);
}

typedef struct edit_line edit_line;
struct edit_line {
    char *content;
    int capacity;
    int length;
    edit_line *next;
    edit_line *prev;
};

#define EDIT_MAX_SEARCH_INPUT_LENGTH 16

typedef struct {
    file_node *node;
    int cursor_col;

    edit_line *root;
    edit_line *first_line;
    edit_line *selected_line;

    const char *tooltip_info;
    int tooltip_info_remaining_time;

    bool search_open;
    char search_input[EDIT_MAX_SEARCH_INPUT_LENGTH];
    size_t search_cursor_position;

    struct {
        Vector2 *items;
        size_t count;
        size_t capacity;
    } search_result;
    size_t selected_search_result;
} edit_process;

int next_power_of_two(int x) {
    if (x == 0) {
        return 1;
    }
    x--;
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;
    x++;
    return x;
}

edit_line *edit_create_line(const char *content) {
    edit_line *line = malloc(sizeof(*line));
    assert(line != NULL);
    line->length = strlen(content);
    int capacity = next_power_of_two(line->length + 1);
    if (capacity < 16) {
        capacity = 16;
    }

    line->content = malloc(capacity);
    strncpy(line->content, content, line->length);
    line->content[line->length] = '\0';
    line->capacity = capacity;
    line->next = NULL;
    line->prev = NULL;
    return line;
}

int edit_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "edit <path>");
        return 1;
    }
    char file[256] = {0};
    strncpy(file, argv[1], 256);
    file_node *node = look_up_node(term->fs.pwd, file);
    if (node == NULL) {
        terminal_append_log(term, TextFormat("File '%s' not found", file));
        return 1;
    }
    if (node->folder) {
        terminal_append_log(term, TextFormat("'%s' is a folder", file));
        return 1;
    }
    edit_process *p = malloc(sizeof(*p));
    assert(p);

    memset(p, 0, sizeof(*p));
    p->node = node;
    term->args = p;

    p->root = edit_create_line(node->lines.items[0]);
    edit_line *last = p->root;

    for (int i = 1; i < node->lines.count; i++) {
        edit_line *new = edit_create_line(node->lines.items[i]);
        new->prev = last;
        last->next = new;
        last = new;
    }
    p->first_line = p->root;
    p->selected_line = p->first_line;
    return 0;
}

void edit_insert_char_at_cursor(edit_process *p, int c) {
    if (!isprint(c)) {
        return;
    }
    edit_line *line = p->selected_line;
    if (line->length == line->capacity) {
        line->capacity *= 2;
        line->content = realloc(line->content, line->capacity);
    }
    int col = fmin(line->length, p->cursor_col);
    memmove(line->content + col + 1, line->content + col, line->length - col);
    line->content[col] = c;
    line->length++;
    line->content[line->length] = '\0';
    p->cursor_col++;
}

bool edit_append_line(edit_process *p, const char *content) {
    edit_line *line = p->selected_line->prev;
    if (line == NULL) {
        return false;
    }

    int length = strlen(content);
    if (line->length + length >= line->capacity) {
        line->capacity = next_power_of_two(line->length + length);
        line->content = realloc(line->content, line->capacity);
    }
    strncat(line->content, content, line->capacity);
    p->cursor_col = line->length;
    line->length += length;
    return true;
}

void edit_save_file(edit_process *p) {
    free_text_lines(&p->node->lines);
    edit_line *line = p->root;
    while (line) {
        append(&p->node->lines, strdup(line->content));
        line = line->next;
    }
    p->tooltip_info = "Saved!";
    p->tooltip_info_remaining_time = 200;
    p->node->content_size = get_lines_total_size(&p->node->lines);
}

void edit_remove_char_at_cursor(edit_process *p) {
    int col = fmin(p->cursor_col, p->selected_line->length);
    if (col == 0) {
        if (edit_append_line(p, p->selected_line->content)) {
            edit_line *prev = p->selected_line->prev;
            prev->next = p->selected_line->next;
            if (p->selected_line->next) {
                p->selected_line->next->prev = prev;
            }
            free(p->selected_line->content);
            free(p->selected_line);
            p->selected_line = prev;
        }
        return;
    }
    edit_line *line = p->selected_line;
    memmove(line->content + col - 1, line->content + col, line->length - col);
    line->length--;
    line->content[line->length] = '\0';
    p->cursor_col = fmin(p->cursor_col - 1, col - 1);
}

int edit_line_offset_prev(const edit_line *first, edit_line *line) {
    int i = 0;
    while (line && line != first) {
        i++;
        line = line->prev;
    }
    assert(line != NULL && "Not found ??");
    return i - 1;
}

void edit_insert_new_line(edit_process *p) {
    int col = fmin(p->cursor_col, p->selected_line->length);
    const char *cut = TextFormat("%.*s", p->selected_line->length - col, p->selected_line->content + col);
    p->selected_line->length = p->cursor_col;
    p->selected_line->content[p->cursor_col] = '\0';
    edit_line *new_line = edit_create_line(cut);
    new_line->next = p->selected_line->next;
    new_line->prev = p->selected_line;
    p->selected_line->next = new_line;
    if (new_line->next) {
        new_line->next->prev = new_line;
    }
    p->selected_line = new_line;

    p->cursor_col = 0;
    int line_position = edit_line_offset_prev(p->first_line, p->selected_line);
    if (line_position > 19) {
        p->first_line = p->first_line->next;
    }
}

edit_line *edit_get_line(edit_process *p, size_t line) {
    edit_line *root = p->root;
    while (root != NULL && line > 0) {
        line--;
        root = root->next;
    }
    if (line != 0) {
        return p->root;
    }
    return root;
}

void edit_set_selected_search_result(edit_process *p, int selected) {
    Vector2 position = p->search_result.items[selected];
    p->selected_search_result = selected;
    p->selected_line = edit_get_line(p, position.x);
    p->first_line = p->selected_line;
    p->cursor_col = position.y;
}

void edit_set_search(edit_process *p) {
    if (p->search_cursor_position == 0) {
        return;
    }
    edit_line *root = p->root;
    char search_term[33] = {0};
    strncpy(search_term, p->search_input, p->search_cursor_position);

    size_t line = 0;
    p->search_result.count = 0;
    p->selected_search_result = 0;

    while (root != NULL) {
        const char *content = root->content;
        const char *search_result;
        while ((search_result = strstr(content, search_term)) != NULL) {
            if (search_result) {
                size_t col = search_result - root->content;
                append(&p->search_result, V(line, col));
                content = search_result + 1;
            }
        }
        root = root->next;
        line++;
    }
    p->search_cursor_position = 0;
    if (p->search_result.count > 0) {
        edit_set_selected_search_result(p, 0);
    }
}

int edit_update(terminal *term) {
    edit_process *p = (edit_process *)term->args;
    term->title = TextFormat("Edit : %s", p->node->name);

    if (p->tooltip_info_remaining_time > 0) {
        p->tooltip_info_remaining_time -= GetFrameTime();
        if (p->tooltip_info_remaining_time <= 0) {
            p->tooltip_info_remaining_time = 0;
            p->tooltip_info = NULL;
        }
    }

    if (term->process_should_exit) {
        edit_line *line = p->root;
        while (line) {
            free(line->content);
            edit_line *next = line->next;
            free(line);
            line = next;
        }
        return 1;
    }
    if (key_pressed_control(KEY_S)) {
        edit_save_file(p);
    }
    if (key_pressed(KEY_RIGHT)) {
        if (p->cursor_col == p->selected_line->length) {
            if (p->selected_line->next) {
                p->selected_line = p->selected_line->next;
                p->cursor_col = 0;
                int line_index = edit_line_offset_prev(p->first_line, p->selected_line);
                if (line_index > 19) {
                    p->first_line = p->first_line->next;
                }
            }
        } else {
            p->cursor_col = fmin(p->cursor_col + 1, p->selected_line->length);
        }
    }
    if (key_pressed(KEY_LEFT)) {
        if (p->cursor_col == 0) {
            if (p->selected_line->prev) {
                if (p->selected_line == p->first_line) {
                    p->first_line = p->selected_line->prev;
                }
                p->selected_line = p->selected_line->prev;
                p->cursor_col = p->selected_line->length;
            }
        } else {
            p->cursor_col = fmin(p->cursor_col, p->selected_line->length);
            p->cursor_col = fmax(p->cursor_col - 1, 0);
        }
    }
    if (key_pressed(KEY_DOWN)) {
        if (p->selected_line->next) {
            p->selected_line = p->selected_line->next;
            int line_index = edit_line_offset_prev(p->first_line, p->selected_line);
            if (line_index > 19) {
                p->first_line = p->first_line->next;
            }
        }
    }
    if (key_pressed(KEY_UP)) {
        if (p->selected_line->prev) {
            if (p->selected_line == p->first_line) {
                p->first_line = p->first_line->prev;
            }
            p->selected_line = p->selected_line->prev;
        }
    }
    if (key_pressed(KEY_BACKSPACE)) {
        if (p->search_open) {
            p->search_cursor_position--;
        } else {
            edit_remove_char_at_cursor(p);
        }
    }
    if (key_pressed(KEY_DELETE)) {
        int col = fmin(p->cursor_col, p->selected_line->length);
        if (col == p->selected_line->length) {
            if (p->selected_line->next) {
                p->selected_line = p->selected_line->next;
                p->cursor_col = 0;
                edit_remove_char_at_cursor(p);
            }
        } else {
            p->cursor_col = fmin(p->cursor_col + 1, p->selected_line->length);
            edit_remove_char_at_cursor(p);
        }
    }
    if (key_pressed(KEY_ENTER)) {
        if (p->search_open) {
            p->search_open = false;
            edit_set_search(p);
        } else {
            edit_insert_new_line(p);
        }
    }

    if (key_pressed_control(KEY_F)) {
        p->search_open = true;
    }

    if (key_pressed_control(KEY_N)) {
        if (p->search_result.count != 0) {
            p->selected_search_result = (p->selected_search_result + 1) % p->search_result.count;
            edit_set_selected_search_result(p, p->selected_search_result);
        }
    }

    if (key_pressed_control(KEY_P)) {
        if (p->search_result.count != 0) {
            p->selected_search_result =
                (p->selected_search_result == 0 ? p->search_result.count : p->selected_search_result) - 1;
            edit_set_selected_search_result(p, p->selected_search_result);
        }
    }

    int key = 0;
    while ((key = GetCharPressed()) != 0) {
        if (p->search_open) {
            if (p->search_cursor_position < EDIT_MAX_SEARCH_INPUT_LENGTH) {
                p->search_input[p->search_cursor_position++] = key;
            }
        } else {
            p->search_result.count = 0;
            edit_insert_char_at_cursor(p, key);
        }
    }
    return 0;
}

void edit_render(terminal *term) {
    edit_process *p = (edit_process *)term->args;

    edit_line *line = p->first_line;
    int i = 0;
    int cursor_row = 0;
    while (line && i <= MAX_LINE_COUNT_PER_SCREEN - 1) {
        DrawTextEx(terminal_font, line->content, S(0, font_size * i), font_size, 1, WHITE);
        line = line->next;
        i++;
        if (line == p->selected_line) {
            cursor_row = i;
        }
    }
    if (!p->search_open) {
        cursor_row = fmin(cursor_row, MAX_LINE_COUNT_PER_SCREEN - 1);
        const char *current_line_content = p->selected_line->content;
        int col = fmin(p->selected_line->length, p->cursor_col);
        Vector2 cursor_pos =
            S(MeasureTextEx(terminal_font, TextFormat("%.*s", col, current_line_content), font_size, 1).x,
              cursor_row * font_size);
        DrawRectangleV(cursor_pos, V(font_size, font_size), WHITE);
        DrawTextEx(terminal_font, TextFormat("%c", current_line_content[col]), cursor_pos, font_size, 1, BLACK);
    } else {
        Vector2 size = {WIDTH * 3 / 4.f, 100};
        Vector2 center = {(WIDTH - size.x) / 2, (HEIGHT - size.y) / 2};
        Rectangle rec = {center.x, center.y, size.x, size.y};
        Rectangle outer = {rec.x - 4, rec.y - 4, rec.width + 8, rec.height + 8};
        DrawRectangleRec(outer, WHITE);
        DrawRectangleRec(rec, BLACK);
        DrawTextEx(terminal_font, TextFormat("%.*s", p->search_cursor_position, p->search_input),
                   (Vector2){rec.x + 16, rec.y + 24}, font_size * 2, 1, WHITE);
    }

    // Tooltip
    Vector2 tooltip_position = S(0, font_size * MAX_LINE_COUNT_PER_SCREEN);
    const char *tooltip_content;
    if (p->search_result.count == 0) {
        tooltip_content = "[C-s] Save [C-c] Exit [C-f] Search";
    } else {
        tooltip_content = TextFormat("Search: %zu [C-n] Next [C-p] Previous", p->search_result.count);
    }
    DrawTextEx(terminal_font, tooltip_content, tooltip_position, font_size, 1, WHITE);

    if (p->tooltip_info) {
        int tooltip_info_length = MeasureTextEx(terminal_font, p->tooltip_info, font_size, 1).x;
        Vector2 tooltip_info_position = S(WIDTH - 60 - tooltip_info_length, font_size * MAX_LINE_COUNT_PER_SCREEN);
        DrawTextEx(terminal_font, p->tooltip_info, tooltip_info_position, font_size, 1, WHITE);
    }
}

int netscan_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "netscan <network>");
        return 1;
    }
    if (is_ip_format(argv[1]) == false) {
        terminal_append_log(term, "Network should be a valid IP address.");
        return 1;
    }
    const char *ip = argv[1];
    const char *network_ip = machine_ip_to_network(ip);
    terminal_append_log(term, TextFormat("Scanning network : %s", network_ip));
    netscan_process *p = malloc(sizeof(*p));
    assert(p);
    p->scanning_network_ip = network_ip;
    p->current_progress = 0;
    p->scantime_left = NETSCAN_SCAN_TIME;
    term->args = p;
    return 0;
}

int netscan_update(terminal *term) {
    netscan_process *p = (netscan_process *)term->args;
    if (term->process_should_exit) {
        free((void *)p->scanning_network_ip);
        return 1;
    }
    if (p->current_progress == 255) {
        for (int i = 0; i < 255; i++) {
            if (p->found[i]) {
                const char *ip = network_ip_get_machine(p->scanning_network_ip, i);
                terminal_append_log(term, TextFormat("Machine %s is reachable", ip));
            }
        }
        free((void *)p->scanning_network_ip);
        return 1;
    }
    p->scantime_left -= GetFrameTime();
    if (p->scantime_left <= 0) {
        p->current_progress++;
        p->scantime_left = NETSCAN_SCAN_TIME;
    }
    term->title = TextFormat("Netscan %s", p->scanning_network_ip);
    const char *scanning_machine_ip = network_ip_get_machine(p->scanning_network_ip, p->current_progress);
    terminal_replace_last_line(term, TextFormat("Scanning %s\n", scanning_machine_ip));
    p->found[p->current_progress] = ip_is_reachable(term, scanning_machine_ip);
    return 0;
}

void put_pixel(int *fb, int x, int y, int color) {
    if (x < 0 || x >= FB_SIZE_WIDTH || y < 0 || y >= FB_SIZE_HEIGHT)
        return;
    fb[y * FB_SIZE_WIDTH + x] = color;
}

typedef enum { TERM_BG, TERM_FG, TERM_BLUE, TERM_GREEN, TERM_RED, TERM_YELLOW, TERM_PURPLE, TERM_COUNT } term_color;

Color cs[TERM_COUNT] = {
    [TERM_BG] = {0, 0, 0, 255},          [TERM_FG] = {255, 255, 255, 255}, [TERM_GREEN] = {0, 228, 48, 255},
    [TERM_RED] = {240, 41, 55, 255},     [TERM_BLUE] = {0, 121, 241, 255}, [TERM_YELLOW] = {253, 249, 0, 255},
    [TERM_PURPLE] = {200, 122, 255, 255}};

void render_framebuffer(int *fb) {
    int cell_width = (WIDTH - 80) / FB_SIZE_WIDTH;
    int cell_height = (HEIGHT - 60) / FB_SIZE_HEIGHT;
    for (int y = 0; y < FB_SIZE_HEIGHT; y++) {
        for (int x = 0; x < FB_SIZE_WIDTH; x++) {
            Color c = cs[fb[y * FB_SIZE_WIDTH + x]];
            DrawRectangle(40 + x * cell_width, 30 + y * cell_height, cell_width, cell_height, c);
        }
    }
}

void put_pixel_fn() {
    exec_process *p = (exec_process *)active_term->args;
    int c = basic_pop_value_num();
    int y = basic_pop_value_num();
    int x = basic_pop_value_num();
    put_pixel(p->fb[1 - p->fb_idx], x, y, c % TERM_COUNT);
    basic_push_int(0);
}

void flip_render_fn() {
    exec_process *p = (exec_process *)active_term->args;
    p->fb_idx = 1 - p->fb_idx;
    active_term->render_not_ready = false;
}

bool terminal_handle_command(const char *cmd);

void system_fn() {
    const char *cmd = basic_pop_value_string();
    int result = terminal_handle_command(cmd) == false ? 1 : 0;
    basic_push_int(result);
    basic_sleep(0.25f);
}

double exec_start = 0;

int exec_init(terminal *t, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(t, "exec <file>");
        return 1;
    }
    const char *filepath = argv[1];
    file_node *file = look_up_node(t->fs.pwd, filepath);
    if (file == NULL || file->folder) {
        terminal_append_log(t, TextFormat("%s is not a file", filepath));
        return 1;
    }
    exec_process *p = malloc(sizeof(*p));
    assert(p != NULL);
    p->filename = strdup(filepath);
    p->fb_idx = 0;
    memset(p->fb[0], 0, sizeof(*p->fb[0]) * FB_SIZE);
    memset(p->fb[1], 0, sizeof(*p->fb[1]) * FB_SIZE);
    const char *program = node_get_content(file);
    // TODO: Avoid additional new line at the end of execution
    terminal_append_log(active_term, "");

    exec_start = GetTime();
    if (!interpreter_init(program, &terminal_basic_print, &terminal_append_print)) {
        return 1;
    }
    register_function("PUTPIXEL", put_pixel_fn, 3);
    register_function("RENDER", flip_render_fn, 0);
    register_function("COLOR_RED", flip_render_fn, 0);
    register_function("SYSTEM", system_fn, 1);

    register_variable_int("COLOR_BG", TERM_BG);
    register_variable_int("COLOR_FG", TERM_FG);
    register_variable_int("COLOR_BLUE", TERM_BLUE);
    register_variable_int("COLOR_GREEN", TERM_GREEN);
    register_variable_int("COLOR_RED", TERM_RED);
    register_variable_int("COLOR_YELLOW", TERM_YELLOW);
    register_variable_int("COLOR_PURPLE", TERM_PURPLE);

    t->render_not_ready = true;
    t->args = p;
    free((void *)program);
    return 0;
}

int exec_update(terminal *term) {
    exec_process *p = (exec_process *)term->args;
    if (term->process_should_exit) {
        interpreter_destroy();
        free((void *)p->filename);
        return 1;
    }
    advance_interpreter_time(GetFrameTime());
    for (int i = 0; i < 100000; i++) {
        if (!step_program()) {
            free((void *)p->filename);
            interpreter_destroy();
            printf("Execution took %f\n", GetTime() - exec_start);
            return 1;
        }
    }
    term->title = TextFormat("Executing %s", p->filename);
    return 0;
}

void exec_render(terminal *term) {
    exec_process *p = (exec_process *)term->args;
    render_framebuffer(p->fb[p->fb_idx]);
}

typedef struct {
    const char *short_subject;
    const char *full_subject;
    const char *from;
    const char *to;
    const char *content[8];
    const char *expected_anwser;
    bool password_found;
} mail;

mail mails[] = {
    {.short_subject = "Intruder",
     .full_subject = "Unauthorized access attempt",
     .from = "boss@xxx.com",
     .to = "jerome@xxx.com",
     .content = {"Hi Jerome,", "There may have been an", "unauthorized access attempt.", "Please check the logs on",
                 "machine2 and send me the", "intruder's IP address.", "Regards,"},
     .expected_anwser = "82.142.23.204"},
};
const size_t mail_count = sizeof(mails) / sizeof(*mails);

int mail_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;
    mail_process *p = malloc(sizeof(*p));
    assert(p);
    p->anwser_ptr = 0;
    p->selected_mail_idx = 0;
    term->args = p;
    return 0;
}

int mail_update(terminal *term) {
    mail_process *p = (mail_process *)term->args;
    term->title = "Mails";
    if (term->process_should_exit) {
        return 1;
    }
    if (key_pressed(KEY_DOWN)) {
        p->selected_mail_idx = fmin(mail_count - 1, p->selected_mail_idx + 1);
        p->anwser_ptr = 0;
    }
    if (key_pressed(KEY_UP)) {
        if (p->selected_mail_idx > 0) {
            p->selected_mail_idx--;
        }
        p->anwser_ptr = 0;
    }

    mail *m = &mails[p->selected_mail_idx];
    if (m->expected_anwser && !m->password_found) {
        int key = 0;
        while ((key = GetCharPressed())) {
            if (p->anwser_ptr < MAIL_PROCESS_ANWSER_MAX_LENGTH) {
                p->anwser[p->anwser_ptr] = key;
                p->anwser_ptr++;
            }
        }
        if (key_pressed(KEY_BACKSPACE)) {
            if (p->anwser_ptr > 0) {
                p->anwser_ptr--;
            }
        }

        if (key_pressed(KEY_ENTER)) {
            if (strlen(m->expected_anwser) == p->anwser_ptr &&
                strncmp(m->expected_anwser, p->anwser, p->anwser_ptr) == 0) {
                m->password_found = true;
            }
        }
    }
    return 0;
}

void mail_render(terminal *term) {
    mail_process *p = (mail_process *)term->args;

    // Layout
    const int split_position = WIDTH * 0.30;
    DrawRectangle(split_position, 0, 15, HEIGHT, WHITE);
    DrawRectangle(split_position, HEIGHT * 0.25, WIDTH, 8, WHITE);

    // Mailbox
    for (size_t i = 0; i < mail_count; i++) {
        const mail *m = &mails[i];
        if (i == p->selected_mail_idx) {
            DrawRectangle(0, 60 + 60 * i, split_position, 60, WHITE);
        } else {
            DrawRectangle(0, 120 + 60 * i, split_position, 8, WHITE);
        }
        DrawTextEx(terminal_font, m->short_subject, (Vector2){60, 80 + 60 * i}, font_size, 1,
                   i == p->selected_mail_idx ? BLACK : WHITE);
    }

    mail *m = &mails[p->selected_mail_idx];
    // Mail header
    const int mail_header_font_size = 26;
    DrawTextEx(terminal_font, TextFormat("From: %s", m->from), (Vector2){split_position + 20, 80},
               mail_header_font_size, 1, WHITE);
    DrawTextEx(terminal_font, TextFormat("To: %s", m->to), (Vector2){split_position + 20, 110}, mail_header_font_size,
               1, WHITE);
    DrawTextEx(terminal_font, m->full_subject, (Vector2){split_position + 20, 140}, mail_header_font_size, 1, WHITE);

    // Body
    for (int i = 0; i < 8; i++) {
        if (m->content[i] == NULL)
            break;
        DrawTextEx(terminal_font, m->content[i], (Vector2){split_position + 20, 200 + 40 * i}, font_size, 1, WHITE);
    }

    // Anwser
    if (m->expected_anwser != NULL) {
        if (!m->password_found) {
            Rectangle rec = {split_position + 50, HEIGHT - 125, (WIDTH - split_position - 125), 75};
            DrawRectangleLinesEx(rec, 8, WHITE);
            DrawTextEx(terminal_font, TextFormat("%.*s", p->anwser_ptr, p->anwser), (Vector2){rec.x + 16, rec.y + 24},
                       font_size, 1, WHITE);
        } else {
            Rectangle rec = {split_position + 50, HEIGHT - 125, (WIDTH - split_position - 125), 75};
            DrawRectangleRec(rec, WHITE);
            DrawTextEx(terminal_font, m->expected_anwser, (Vector2){rec.x + 16, rec.y + 24}, font_size, 1, BLACK);
        }
    }
}

int list_init(terminal *term, int argc, const char **argv) {
    file_node *root = active_term->fs.pwd;
    if (argc == 2) {
        root = look_up_node(root, argv[1]);
    } else if (argc > 2) {
        terminal_append_log(term, "list (path)");
        return 1;
    }
    if (root == NULL) {
        terminal_append_log(term, TextFormat("Unknown file : %s", argv[1]));
        return 1;
    }
    terminal_append_log(term, "Type Name             Size");
    terminal_append_log(term, "-----------------------------------");

    if (root->folder == false) {
        // TODO: Suffixed size
        const char *str = TextFormat("f    %-16s %-4d bytes", root->name, root->content_size);
        terminal_append_log(term, str);
        return 0;
    }

    for (int i = 0; i < root->children.count; i++) {
        file_node *node = root->children.items[i];
        if (node->folder) {
            const char *str = TextFormat("d    %-16s %-4d children", node->name, node->children.count);
            terminal_append_log(term, str);
        } else {
            // TODO: Suffixed size
            const char *str = TextFormat("f    %-16s %-4d bytes", node->name, node->content_size);
            terminal_append_log(term, str);
        }
    }
    return 0;
}

int cd_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "cd <folder>");
        return 1;
    }
    file_node *node = look_up_node(term->fs.pwd, argv[1]);
    if (node) {
        if (node->folder) {
            term->fs.pwd = node;
        } else {
            terminal_append_log(term, TextFormat("'%s' is a file", argv[1]));
        }
    } else {
        terminal_append_log(term, TextFormat("Folder '%s' not found", argv[1]));
    }
    return 0;
}

int echo_init(terminal *term, int argc, const char **argv) {
    if (argc > 1) {
        terminal_append_log(term, "");
        for (int i = 1; i < argc - 1; i++) {
            terminal_log_append_text(term, TextFormat("%s ", argv[i]));
        }
        terminal_log_append_text(term, argv[argc - 1]);
    }
    return 0;
}

int print_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "print <file>");
        return 1;
    }
    file_node *node = look_up_node(term->fs.pwd, argv[1]);
    if (node && node->folder == false) {
        for (int i = 0; i < node->lines.count; i++) {
            const char *line = node->lines.items[i];
            while (true) {
                int line_length = TextFindIndex(line, "\n");
                if (line_length == -1) {
                    break;
                }
                terminal_append_log(term, TextFormat("%.*s", line_length, line));
                line += line_length + 1;
            }
            if (*line != '\0') {
                terminal_append_log(term, TextFormat("%s", line));
            }
        }
    } else {
        terminal_append_log(term, TextFormat("File '%s' not found", argv[1]));
    }
    return 0;
}

int path_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "path <file>");
        return 1;
    }
    file_node *node = look_up_node(term->fs.pwd, argv[1]);
    if (node) {
        terminal_append_log(term, get_file_full_path(node));
    } else {
        terminal_append_log(term, TextFormat("Unknown file %s", argv[1]));
    }
    return 0;
}

int pwd_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;
    terminal_append_log(term, get_file_full_path(term->fs.pwd));
    return 0;
}

int create_init(terminal *term, int argc, const char **argv) {
    if (argc != 3) {
        terminal_append_log(term, "create (d|f) <path>");
        return 1;
    }
    if (look_up_node(term->fs.pwd, argv[2]) != NULL) {
        terminal_append_log(term, TextFormat("Path %s already exists", argv[2]));
        return 1;
    }
    // TODO: Don't use argv[2] directly because it allows naming files "a/b"
    if (strcmp(argv[1], "d") == 0) {
        file_node_append_children(term->fs.pwd, folder_new(argv[2]));
    } else if (strcmp(argv[1], "f") == 0) {
        file_node_append_children(term->fs.pwd, file_new(argv[2], (text_lines){0}));
    } else {
        terminal_append_log(term, "create (d|f) <path>");
        return 1;
    }
    return 0;
}

int connect_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "connect <ip>");
        return 1;
    }
    const char *ip = argv[1];
    bool found = false;
    terminal *other = NULL;
    for (int i = 0; i < MAX_NETWORK_COUNT; i++) {
        other = network_get_terminal(&networks[i], ip);
        const terminal *me = network_get_terminal(&networks[i], active_term->ip);
        if (other != NULL && me != NULL) {
            found = true;
            break;
        }
    }
    if (found && other) {
        active_term = other;
        active_term->connected = true;
        terminal_append_log(term, TextFormat("Machine %s online", ip));
    } else {
        terminal_append_log(term, TextFormat("Machine %s offline", ip));
    }
    return 0;
}

int machines_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;
    machines_process *p = malloc(sizeof(*p));
    assert(p != NULL);
    p->selected_index = 0;
    p->machines.items = NULL;
    p->machines.count = 0;
    p->machines.capacity = 0;
    assert(p != NULL);
    for (int i = 0; i < terminal_count; i++) {
        if (all_terminals[i].connected) {
            append(&p->machines, &all_terminals[i]);
            if (&all_terminals[i] == active_term) {
                p->selected_index = i;
            }
        }
    }
    term->args = p;
    first_frame_enter = true;
    return 0;
}

int machines_update(terminal *term) {
    machines_process *p = (machines_process *)term->args;
    if (term->process_should_exit) {
        free(p->machines.items);
        return 1;
    }

    if (key_pressed(KEY_ENTER)) {
        active_term = p->machines.items[p->selected_index];
        free(p->machines.items);
        return 1;
    }
    if (key_pressed(KEY_DOWN)) {
        p->selected_index = fmin(p->selected_index + 1, p->machines.count - 1);
    }
    if (key_pressed(KEY_UP)) {
        p->selected_index = fmax(p->selected_index - 1, 0);
    }
    term->title = "Switch to another connected machine";
    first_frame_enter = false;
    return 0;
}

void machines_render(terminal *term) {
    machines_process *p = (machines_process *)term->args;
    for (int i = 0; i < p->machines.count; i++) {
        terminal *t = p->machines.items[i];
        const char *text = TextFormat("%s%s@%s", t == active_term ? "> " : "", t->hostname, t->ip);
        if (i == p->selected_index) {
            DrawHoveredTerminalLine(text, i);
        } else {
            DrawTerminalLine(text, i);
        }
    }
}

int hostname_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;
    terminal_append_log(term, term->hostname);
    return 0;
}

int shutdown_init(terminal *term, int argc, const char **argv) {
    (void)term;
    (void)argc;
    (void)argv;
    exit(0);
}

int clear_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;

    for (size_t i = 0; i < active_term->log_count; i++) {
        size_t index = (term->log_head + MAX_LOG_COUNT - term->log_count + i) % MAX_LOG_COUNT;
        free((void *)term->logs[index]);
        term->logs[index] = NULL;
    }
    term->log_head = 0;
    term->log_tail = 0;
    term->log_count = 0;
    return 0;
}

int ping_init(terminal *term, int argc, const char **argv) {
    if (argc != 2) {
        terminal_append_log(term, "ping <ip>");
        return 1;
    }
    const char *ip = strdup(argv[1]);
    if (is_ip_format(ip) == false) {
        terminal_append_log(term, "Invalid IP format");
        return 1;
    }

    term->title = "Ping";

    const terminal *other = network_get_terminal(&networks[0], ip);
    if (other) {
        terminal_append_log(term, TextFormat("Machine %s online", ip));
    } else {
        terminal_append_log(term, TextFormat("Machine %s offline", ip));
    }

    return 0;
}

int help_init(terminal *term, int argc, const char **argv) {
    int default_selected = 0;
    if (argc >= 2) {
        const char *asked_command = argv[1];
        default_selected = get_command_id(asked_command);
        if (default_selected == -1) {
            terminal_append_log(term, TextFormat("Unknown command : %s.", asked_command));
            terminal_append_log(active_term, "Type 'help' to see available commands");
            return 1;
        }
    }

    help_process *p = malloc(sizeof(*p));
    assert(p != NULL);
    p->selected_index = default_selected;
    p->offset = fmin(default_selected, command_count - 10);
    term->args = p;
    return 0;
}

int help_update(terminal *term) {
    help_process *p = (help_process *)term->args;
    if (term->process_should_exit) {
        return 1;
    }
    term->title = TextFormat("Help : %s", commands[p->selected_index].name);
    if (key_pressed(KEY_DOWN)) {
        p->selected_index = fmin(p->selected_index + 1, command_count - 1);
        if (p->selected_index - p->offset >= 10) {
            p->offset = fmin(p->offset + 1, command_count - 10);
        }
    }
    if (key_pressed(KEY_UP)) {
        p->selected_index = fmax(p->selected_index - 1, 0);
        if (p->selected_index == p->offset - 1) {
            p->offset = fmax(p->offset - 1, 0);
        }
    }
    return 0;
}

void help_render(terminal *term) {
    const help_process *p = (help_process *)term->args;

    // Layout
    const int split_position = WIDTH * 0.30;
    DrawRectangle(split_position, 60, 15, HEIGHT, WHITE);

    // Doc render
    for (size_t i = p->offset, j = 0; i < command_count; i++, j++) {
        const command *c = &commands[i];
        Color text_color = (int)i == p->selected_index ? BLACK : WHITE;
        if ((int)i == p->selected_index) {
            DrawRectangle(0, 60 + 60 * j, split_position, 60, WHITE);
        } else {
            DrawRectangle(0, 120 + 60 * j, split_position, 8, WHITE);
        }
        DrawTextEx(terminal_font, c->name, (Vector2){60, 80 + 60 * j}, font_size, 1, text_color);
    }
    const command *selected_command = &commands[p->selected_index];
    DrawTextEx(terminal_font, get_command_help(selected_command->name), (Vector2){split_position + 20, 80}, font_size,
               1, WHITE);
}

bool terminal_handle_command(const char *cmd) {
    int argc = 0;
    const char **argv = TextSplit(cmd, ' ', &argc);
    if (argc == 0 || *argv[0] == '\0') {
        return false;
    }

    command *c = NULL;
    for (size_t i = 0; i < command_count; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            c = &commands[i];
            break;
        } else if (commands[i].alias != NULL && strcmp(argv[0], commands[i].alias) == 0) {
            c = &commands[i];
            break;
        }
    }

    if (c != NULL) {
        if (c->init(active_term, argc, argv) != 0) {
            return false;
        }
        if (c->update != NULL) {
            active_term->process_update = c->update;
        }
        if (c->render != NULL) {
            active_term->process_render = c->render;
        }
        return true;
    }
    terminal_append_log(active_term, TextFormat("Unknown command : %s", argv[0]));
    terminal_append_log(active_term, "Type 'help' to see available commands");
    return false;
}

void terminal_exec_input(terminal *term) {
    term->history_ptr = 0;
    const char *input = TextFormat("%.*s", term->input_cursor, term->input);
    terminal_append_input(term);
    terminal_handle_command(input);
}

const char *terminal_autocomplete_command(const char *input) {
    size_t input_length = strlen(input);
    const char *completed_command = NULL;
    for (size_t i = 0; i < command_count; i++) {
        if (input_length <= strlen(commands[i].name)) {
            if (strncmp(input, commands[i].name, input_length) == 0) {
                completed_command = commands[i].name;
                break;
            }
        }
    }
    return completed_command;
}

const char *terminal_autocomplete_file(file_node *root, const char *path) {
    file_node *node = get_parent_from_path(root, path);
    if (node == NULL) {
        return NULL;
    }
    const char *input_filename = get_path_filename(path);
    size_t path_length = strlen(input_filename);
    const char *completed_file_path = NULL;
    for (int i = 0; i < node->children.count; i++) {
        const char *file_name = node->children.items[i]->name;
        if (path_length <= strlen(file_name)) {
            if (strncmp(input_filename, file_name, path_length) == 0) {
                completed_file_path = file_name + path_length;
                break;
            }
        }
    }
    return completed_file_path;
}

// TODO: Cycle
void terminal_autocomplete_input(terminal *term) {
    if (term->input_cursor == 0) {
        return;
    }

    const char *input = TextFormat("%.*s", term->input_cursor, term->input);
    int space_offset = TextFindIndex(input, " ");

    // User is typing the command name
    if (space_offset == -1) {
        const char *completed_command = terminal_autocomplete_command(input);
        if (completed_command != NULL) {
            strncpy(term->input, completed_command, MAX_INPUT_LENGTH);
            term->input_cursor = strlen(completed_command);
        }
    } else {
        const char *base = input + space_offset + 1;
        while ((space_offset = TextFindIndex(base, " ")) != -1) {
            base = base + space_offset + 1;
        }
        const char *completed_file_path = terminal_autocomplete_file(term->fs.pwd, base);
        if (completed_file_path != NULL) {
            size_t max_length = MAX_INPUT_LENGTH - term->input_cursor;
            strncpy(term->input + term->input_cursor, completed_file_path, max_length);
            term->input_cursor += strlen(completed_file_path);
            term->input_cursor = fmin(MAX_INPUT_LENGTH, term->input_cursor);
        }
    }
}

bool TextEmpty(const char *s) {
    while (*s) {
        if (!isspace(*s)) {
            return false;
        }
        s++;
    }
    return true;
}

void load_machines() {
    char *file_content = LoadFileText("assets/machines");
    text_lines lines = text_split(file_content, '\n');
    terminal_count = TextToInteger(lines.items[0]);
    all_terminals = calloc(terminal_count, sizeof(terminal));
    int machine_idx = -1;

    bool parsing_files = false;
    for (int i = 1; i < lines.count; i++) {
        if (strcmp(lines.items[i], "---") == 0) {
            machine_idx++;
            parsing_files = false;
            continue;
        }
        if (TextEmpty(lines.items[i])) {
            continue;
        }
        if (parsing_files) {
            if (lines.items[i][0] == '-') {
                lines.items[i] += 2;
                int split_index = TextFindIndex(lines.items[i], ":");
                if (!split_index) {
                    printf("Error parsing file at line %d\n", i);
                    exit(1);
                }
                char *file = (char *)TextSubtext(lines.items[i], 0, split_index);
                const char *content = lines.items[i] + split_index + 1;
                if (file[strlen(file) - 1] == '/') {
                    file[strlen(file) - 1] = '\0';
                    file_node_append_folder_full_path(all_terminals[machine_idx].fs.root, file);
                } else {
                    char *content_with_new_lines = TextReplace(content, "\\n", "\n");
                    text_lines file_lines = text_split(strdup(content_with_new_lines), '\n');
                    file_node_append_file_full_path(all_terminals[machine_idx].fs.root, file, file_lines);
                }
            }
            continue;
        }
        terminal *current = &all_terminals[machine_idx];
        int split_index = TextFindIndex(lines.items[i], ": ");
        if (split_index) {
            const char *key = TextSubtext(lines.items[i], 0, split_index);
            const char *value = lines.items[i] + split_index + 2;
            if (strcmp(key, "hostname") == 0) {
                current->hostname = strdup(value);
            } else if (strcmp(key, "ip") == 0) {
                if (!is_ip_format(value)) {
                    printf("Wrong format for ip : '%s'\n", value);
                    exit(1);
                }
                current->ip = strdup(value);
            } else if (strcmp(key, "files") == 0) {
                parsing_files = true;
                current->fs.root = folder_new("/");
                current->fs.root->parent = current->fs.root;
                current->fs.pwd = current->fs.root;
            } else if (strcmp(key, "network") == 0) {
                if (all_terminals[machine_idx].ip == NULL) {
                    printf("IP should be set before network for machine %d\n", machine_idx);
                    exit(1);
                }
                while (*value) {
                    if (*value >= '0' && *value <= '9') {
                        network_append_machine(&networks[*value - '0'], current);
                    }
                    value++;
                }
            } else {
                printf("Unknown machine property: %s:%s", key, value);
                exit(1);
            }
        }
    }
    free(file_content);
}

int bootup_sequence_idx = -1;
float bootup_sequence_line_time_left = 1000;

int bootup_sequence_update(terminal *term) {
#if DISABLE_BOOTUP_SEQUENCE
    return 1;
#endif
    term->title = NULL;
    bootup_sequence_line_time_left -= GetFrameTime() * 1000;
    if (bootup_sequence_line_time_left < 0) {
        bootup_sequence_idx++;
        bootup_sequence_line *line = bootseq_get_line(bootup_sequence_idx);
        if (line == NULL) {
            return 1;
        }
        bootup_sequence_line_time_left = line->waiting_time;
        if (line->callback) {
            line->callback();
        }
    }
    return 0;
}

void bootup_sequence_render(terminal *term) {
    (void)term;

    int real_line_count = 0;
    for (int i = 0; i <= bootup_sequence_idx; i++) {
        bootup_sequence_line *line = bootseq_get_line(i);
        if (line->override_previous == false) {
            real_line_count++;
        }
    }

    int line = -1;
    int start_line = fmax(0, real_line_count - MAX_LINE_COUNT_PER_SCREEN);

    int real_start = 0;
    int current_line = 0;
    for (int i = 0; i < bootup_sequence_idx; i++) {
        bootup_sequence_line *seq_line = bootseq_get_line(i);
        if (seq_line->override_previous == false) {
            current_line++;
        }
        if (current_line == start_line) {
            real_start = i;
            break;
        }
    }

    for (int i = real_start; i <= bootup_sequence_idx; i++) {
        bootup_sequence_line *seq_line = bootseq_get_line(i);
        if (seq_line->override_previous == false) {
            line++;
        }
        DrawTerminalLine(seq_line->content, line - 1);
    }

    if (bootup_sequence_idx >= 0) {
        bootup_sequence_line *seq_line = bootseq_get_line(bootup_sequence_idx);
        if ((int)GetTime() % 2 == 0) {
            int x_offset = seq_line->content[0] == '\0' ? 0 : 8;
            int x = x_offset + MeasureTextEx(terminal_font, seq_line->content, font_size, 1).x;
            int y = font_size * (line - 1);
            DrawRectangleV(S(x, y), V(10, font_size), WHITE);
        }
    }
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WIDTH, HEIGHT, "TUIGame");
    InitAudioDevice();
    SetTargetFPS(60);
    SetExitKey(0);

    terminal_shader = LoadShader(0, "shaders/pixeliser.fs");
    target = LoadRenderTexture(WIDTH, HEIGHT);
    SetTextureFilter(target.texture, TEXTURE_FILTER_POINT);
    terminal_font = LoadFont("fonts/C64_TrueType_v1.2.1-STYLE/fonts/C64_Pro_Mono-STYLE.ttf");

    int time = GetShaderLocation(terminal_shader, "time");
    int width = GetShaderLocation(terminal_shader, "renderWidth");
    int height = GetShaderLocation(terminal_shader, "renderHeight");
    SetShaderValue(terminal_shader, width, &WIDTH, SHADER_ATTRIB_FLOAT);
    SetShaderValue(terminal_shader, height, &HEIGHT, SHADER_ATTRIB_FLOAT);

    load_machines();

    active_term = &all_terminals[0];
    active_term->process_update = bootup_sequence_update;
    active_term->process_render = bootup_sequence_render;
    active_term->connected = true;
    bootseq_init();
    terminal_append_log(active_term, "You have (1) unread mail.");

    while (!WindowShouldClose()) {
        first_frame_enter = false;
        if (active_term->process_update == NULL) {
            int key = 0;
            if (IsKeyDown(KEY_LEFT_CONTROL) == false) {
                while ((key = GetCharPressed()) != 0) {
                    if (active_term->input_cursor < MAX_INPUT_LENGTH) {
                        active_term->input[active_term->input_cursor] = key;
                        active_term->input_cursor++;
                    }
                }
            }
            if (key_pressed(KEY_BACKSPACE) && active_term->input_cursor > 0) {
                active_term->input_cursor--;
            }
            if (key_pressed(KEY_ENTER)) {
                first_frame_enter = true;
                terminal_exec_input(active_term);
            }
            if (key_pressed_control(KEY_UP)) {
                if (active_term->scroll_offset > 0) {
                    active_term->scroll_offset--;
                }
            } else if (key_pressed(KEY_UP)) {
                int offset = active_term->history.count - active_term->history_ptr - 1;
                if (offset >= 0) {
                    strncpy(active_term->input, active_term->history.items[offset], MAX_INPUT_LENGTH);
                    active_term->input_cursor = strlen(active_term->input);
                    active_term->history_ptr++;
                }
                // active_term->scroll_offset = fmax(0, active_term->log_count - MAX_LINE_COUNT_PER_SCREEN);
            }
            if (key_pressed_control(KEY_DOWN)) {
                active_term->scroll_offset =
                    fmin(active_term->scroll_offset + 1, fmax(0, active_term->log_count - MAX_LINE_COUNT_PER_SCREEN));
            } else if (key_pressed(KEY_DOWN)) {
                if (active_term->history_ptr > 1) {
                    active_term->history_ptr--;
                    int offset = active_term->history.count - active_term->history_ptr;
                    strncpy(active_term->input, active_term->history.items[offset], MAX_INPUT_LENGTH);
                    active_term->input_cursor = strlen(active_term->input);
                } else {
                    active_term->history_ptr = 0;
                    active_term->input_cursor = 0;
                }
                // active_term->scroll_offset = fmax(0, active_term->log_count - MAX_LINE_COUNT_PER_SCREEN);
            }
            if (key_pressed(KEY_TAB)) {
                terminal_autocomplete_input(active_term);
            }
            if (key_pressed_control(KEY_L)) {
                clear_init(active_term, 0, NULL);
            }
        }

        if (active_term->process_update != NULL) {
            active_term->process_should_exit = key_pressed_control(KEY_C);
            if (active_term->process_update(active_term)) {
                active_term->process_update = NULL;
                active_term->process_render = NULL;
                active_term->render_not_ready = false;
                free(active_term->args);
                active_term->args = NULL;
                active_term->process_should_exit = false;
            }
        } else {
            active_term->title = TextFormat("%s : %s", active_term->hostname, active_term->ip);
        }

        float total_time = GetTime();
        SetShaderValue(terminal_shader, time, &total_time, SHADER_ATTRIB_FLOAT);
        BeginTextureMode(target);
        {
            ClearBackground(BLACK);
            if (active_term->title != NULL) {
                DrawRectangle(0, 0, WIDTH, 35 + font_size, WHITE);
                int middle = (WIDTH - MeasureTextEx(terminal_font, active_term->title, 28, 1).x) / 2;
                DrawTextEx(terminal_font, active_term->title, (Vector2){middle, 30}, 28, 1, BLACK);
            }

            if (active_term->process_render != NULL && !active_term->render_not_ready) {
                active_term->process_render(active_term);
            } else {
                terminal_render(active_term);
            }
            if (active_term->process_update == NULL) {
                bool is_scrolling =
                    active_term->log_count >= MAX_LINE_COUNT_PER_SCREEN &&
                    (active_term->scroll_offset != (int)active_term->log_count - MAX_LINE_COUNT_PER_SCREEN);
                if (!is_scrolling) {
                    terminal_render_prompt(active_term);
                }
            }
        }
        EndTextureMode();

        BeginDrawing();
        {
            ClearBackground((Color){0.788 * 255, 0.780 * 255, 0.685 * 255, 255});
            BeginShaderMode(terminal_shader);
            float scale = fminf((float)GetScreenWidth() / WIDTH, (float)GetScreenHeight() / HEIGHT);
            int offset_x = (GetScreenWidth() - (WIDTH * scale)) / 2;
            int offset_y = (GetScreenHeight() - (HEIGHT * scale)) / 2;
            Rectangle src = {0, 0, WIDTH, -HEIGHT};
            Rectangle dest = {offset_x, offset_y, (WIDTH * scale), (HEIGHT * scale)};
            DrawTexturePro(target.texture, src, dest, (Vector2){0}, 0, WHITE);
            EndShaderMode();
        }
        DrawFPS(0, 0);
        EndDrawing();
    }
    CloseAudioDevice();
    CloseWindow();
}
