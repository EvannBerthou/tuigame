#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#define FB_SIZE FB_SIZE_WIDTH *FB_SIZE_HEIGHT

Shader terminal_shader = {0};
RenderTexture2D target = {0};
Font terminal_font = {0};

typedef struct {
    const char *ip;
    float remaining_time;
    int remaning_count;
} ping_process;

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
    basic_interpreter interpreter;
    int fb[2][FB_SIZE];
    int fb_idx;
} exec_process;

#define MAX_INPUT_LENGTH 40
#define MAX_LINE_COUNT_PER_SCREEN 21

typedef struct file_node file_node;

struct file_node {
    file_node *parent;
    const char *name;
    bool folder;
    char *content;
    file_node **children;
    int children_capacity;
    int children_count;
};

void file_node_append_children(file_node *root, file_node *children) {
    if (root->folder == false) {
        return;
    }
    if (root->children_count == root->children_capacity) {
        root->children_capacity = root->children_capacity == 0 ? 8 : root->children_capacity * 2;
        root->children = realloc(root->children, sizeof(file_node *) * root->children_capacity);
    }
    children->parent = root;
    root->children[root->children_count] = children;
    root->children_count++;
}

file_node *file_new(const char *name, const char *content) {
    file_node *result = malloc(sizeof(file_node));
    assert(result != NULL);
    result->parent = NULL;
    result->name = strdup(name);
    result->content = strdup(content);
    result->folder = false;
    result->children = NULL;
    result->children_count = 0;
    result->children_capacity = 0;
    return result;
}

file_node *folder_new(const char *name) {
    file_node *result = malloc(sizeof(file_node));
    assert(result != NULL);
    result->parent = NULL;
    result->name = strdup(name);
    result->content = NULL;
    result->folder = true;
    result->children = NULL;
    result->children_count = 0;
    result->children_capacity = 0;
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

    for (int i = 0; i < root->children_count; i++) {
        if (strcmp(filename, root->children[i]->name) == 0) {
            return root->children[i];
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

void file_node_append_file_full_path(file_node *root, const char *path, const char *content) {
    int idx = 0;
    do {
        int next_directory = TextFindIndex(path + idx, "/");
        if (next_directory == -1) {
            break;
        }
        idx += next_directory + 1;
    } while (true);
    file_node *parent = look_up_node(root, TextFormat("%.*s", idx, path));
    file_node_append_children(parent, file_new(path + idx, content));
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

typedef struct terminal terminal;
typedef int (*process_init)(terminal *term, int argc, const char **argv);
typedef int (*process_update)(terminal *term, void *args);
typedef void (*process_render)(terminal *term, void *args);

struct terminal {
    const char *title;
    const char *hostname;
    const char *ip;

    const char **logs;
    int log_capacity;
    int log_count;

    // TODO: Cursor mouvements
    char input[MAX_INPUT_LENGTH + 1];
    int input_cursor;

    const char **history;
    int history_capacity;
    int history_count;
    int history_ptr;

    int offset;
    process_update process_update;
    process_render process_render;
    void *args;

    filesystem fs;
};

const int font_size = 28;
terminal *all_terminals = NULL;
terminal *active_term = NULL;
int terminal_count = 0;

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
//   script ? -> Scripting language

// TODO: Command improvements
//   ls -> Add date
//   edit -> Search

#define COMMANDS       \
    XI(list, "ls")     \
    XI(cd, NULL)       \
    XI(echo, NULL)     \
    XI(print, NULL)    \
    XI(path, NULL)     \
    XI(pwd, NULL)      \
    XI(create, NULL)   \
    XI(connect, NULL)  \
    XI(hostname, NULL) \
    XIUR(mail, NULL)   \
    XI(shutdown, NULL) \
    XI(clear, NULL)    \
    XIU(ping, NULL)    \
    XIUR(edit, NULL)   \
    XIUR(help, NULL)   \
    XIUR(test, NULL)   \
    XIUR(exec, NULL)   \
    XIU(netscan, "ns")

#define XI(n, a) int n##_init(terminal *term, int argc, const char **argv);
#define XIU(n, a)                                              \
    int n##_init(terminal *term, int argc, const char **argv); \
    int n##_update(terminal *term, void *args);
#define XIUR(n, a) XIU(n, a) void n##_render(terminal *term, void *args);
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

const size_t command_count = sizeof(commands) / sizeof(commands[0]);

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

void DrawTerminalLine(const char *text, int line) {
    DrawTextEx(terminal_font, text, (Vector2){60, 40 + font_size * (line + 1)}, font_size, 1, WHITE);
}

void terminal_append_log(terminal *term, const char *text) {
    if (term->log_count == term->log_capacity) {
        term->log_capacity = term->log_capacity == 0 ? 8 : term->log_capacity * 2;
        term->logs = realloc(term->logs, sizeof(*term->logs) * term->log_capacity);
    }
    term->logs[term->log_count] = strdup(text);
    term->log_count++;
    term->offset = fmax(term->log_count - MAX_LINE_COUNT_PER_SCREEN, 0);
}

void terminal_log_append_text(terminal *term, const char *text) {
    const char *last_line = term->logs[term->log_count - 1];
    int current_len = strlen(last_line);
    int new_text_len = strlen(text);
    int new_len = current_len + new_text_len + 1;
    char *new_line = malloc(new_len);
    assert(new_line != NULL);
    memset(new_line, 0, new_len);
    strncpy(new_line, last_line, current_len);
    strncat(new_line, text, new_text_len);
    free((void *)last_line);
    term->logs[term->log_count - 1] = new_line;
}

void terminal_replace_last_line(terminal *term, const char *text) {
    if (term->log_count == 0) {
        terminal_append_log(term, text);
        return;
    }

    char *last_line = (char *)term->logs[term->log_count - 1];
    size_t last_line_len = strlen(last_line) + 1;
    size_t new_len = strlen(text) + 1;
    if (last_line_len < new_len) {
        term->logs[term->log_count - 1] = realloc(last_line, new_len);
    }
    strncpy(last_line, text, last_line_len);
}

void terminal_basic_print(const char *text) {
    printf("%s", text);
    if (*text == '\n') {
        terminal_append_log(active_term, "");
    } else {
        terminal_append_log(active_term, text);
    }
}

void terminal_append_print(const char *text) {
    printf("[Basic]: %s", text);
    terminal_log_append_text(active_term, text);
}

void terminal_append_input(terminal *term) {
    terminal_append_log(term, TextFormat("$%.*s", term->input_cursor, term->input));
    if (term->history_count == term->history_capacity) {
        term->history_capacity = term->history_capacity == 0 ? 8 : term->history_capacity * 2;
        term->history = realloc(term->history, sizeof(*term->history) * term->history_capacity);
    }
    term->history[term->history_count] = strdup(TextFormat("%.*s", term->input_cursor, term->input));
    term->history_count++;
    term->input_cursor = 0;
}

void terminal_render(const terminal *term) {
    for (int i = term->offset; i < term->log_count; i++) {
        DrawTerminalLine(term->logs[i], i - term->offset);
    }
}

void terminal_render_prompt(terminal *term) {
    int last_line_index = fmin(MAX_LINE_COUNT_PER_SCREEN, term->log_count);
    const char *prompt_text = TextFormat("$%.*s", term->input_cursor, term->input);
    DrawTerminalLine(prompt_text, last_line_index);
    // TODO: Blink qu'après 1 seconde d'inactivité
    if ((int)GetTime() % 2 == 0) {
        DrawRectangle(60 + MeasureTextEx(terminal_font, prompt_text, font_size, 1).x + 8,
                      40 + font_size * (last_line_index + 1) + 4, 10, font_size - 8, WHITE);
    }
}

int ping_update(terminal *term, void *args) {
    ping_process *p = (ping_process *)args;
    term->title = TextFormat("Ping : %s", p->ip);
    p->remaining_time -= GetFrameTime();
    if (p->remaining_time <= 0) {
        const terminal *other = network_get_terminal(&networks[0], p->ip);
        if (other) {
            terminal_append_log(term, TextFormat("Machine %s online", p->ip));
        } else {
            terminal_append_log(term, TextFormat("Machine %s offline", p->ip));
        }
        p->remaning_count--;
        p->remaining_time = 1.f;
        if (p->remaning_count == 0) {
            free((void *)p->ip);
            return 1;
        }
    }
    return 0;
}

typedef struct edit_line edit_line;
struct edit_line {
    char *content;
    int capacity;
    int length;
    edit_line *next;
    edit_line *prev;
};

typedef struct {
    file_node *node;
    int cursor_col;

    edit_line *root;
    edit_line *first_line;
    edit_line *selected_line;

    const char *tooltip_info;
    int tooltip_info_remaining_time;
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

// TODO: Hack because KEY_ENTER is true on first frame and inserts a new line
int edit_first_frame = true;

edit_line *edit_create_line(const char *content) {
    edit_line *line = malloc(sizeof(edit_line));
    assert(line != NULL);
    line->length = strlen(content);
    int capacity = next_power_of_two(line->length);
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
    edit_process *p = malloc(sizeof(edit_process));
    assert(p);
    p->node = node;
    p->cursor_col = 0;
    p->tooltip_info = NULL;
    p->tooltip_info_remaining_time = 0;
    term->args = p;

    int line_count = 0;
    const char **lines = TextSplit(node->content, '\n', &line_count);
    assert(line_count < 256);
    edit_first_frame = true;

    p->root = edit_create_line(lines[0]);
    edit_line *last = p->root;

    for (int i = 1; i < line_count; i++) {
        edit_line *new = edit_create_line(lines[i]);
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
    int total_length = -1;
    edit_line *line = p->root;
    while (line) {
        total_length += line->length + 1;
        line = line->next;
    }
    total_length = fmax(1, total_length);
    p->node->content = realloc((void *)p->node->content, total_length);
    int write_idx = 0;
    line = p->root;
    while (line) {
        strncpy((void *)(p->node->content + write_idx), line->content, line->length + 1);
        write_idx += line->length;
        p->node->content[write_idx] = '\n';
        write_idx++;
        line = line->next;
    }
    p->node->content[total_length] = '\0';
    p->tooltip_info = "Saved!";
    p->tooltip_info_remaining_time = 200;
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

int edit_update(terminal *term, void *args) {
    edit_process *p = (edit_process *)args;
    term->title = TextFormat("Edit : %s", p->node->name);

    if (p->tooltip_info_remaining_time > 0) {
        p->tooltip_info_remaining_time -= GetFrameTime();
        if (p->tooltip_info_remaining_time <= 0) {
            p->tooltip_info_remaining_time = 0;
            p->tooltip_info = NULL;
        }
    }

    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_A)) {
        edit_line *line = p->root;
        while (line) {
            free(line->content);
            edit_line *next = line->next;
            free(line);
            line = next;
        }
        return 1;
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_S)) {
        edit_save_file(p);
    }
    if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
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
    if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
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
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        if (p->selected_line->next) {
            p->selected_line = p->selected_line->next;
            int line_index = edit_line_offset_prev(p->first_line, p->selected_line);
            if (line_index > 19) {
                p->first_line = p->first_line->next;
            }
        }
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        if (p->selected_line->prev) {
            if (p->selected_line == p->first_line) {
                p->first_line = p->first_line->prev;
            }
            p->selected_line = p->selected_line->prev;
        }
    }
    if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
        edit_remove_char_at_cursor(p);
    }
    if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
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
    if (edit_first_frame == false && IsKeyPressed(KEY_ENTER)) {
        edit_insert_new_line(p);
    }

    int key = 0;
    while ((key = GetCharPressed()) != 0) {
        edit_insert_char_at_cursor(p, key);
    }
    edit_first_frame = false;
    return 0;
}

void edit_render(terminal *term, void *args) {
    (void)term;
    edit_process *p = (edit_process *)args;

    edit_line *line = p->first_line;
    int i = 0;
    int cursor_row = 0;
    while (line && i <= 20) {
        DrawTextEx(terminal_font, line->content, (Vector2){60, 68 + 28 * i}, 28, 1, WHITE);
        line = line->next;
        i++;
        if (line == p->selected_line) {
            cursor_row = i;
        }
    }
    cursor_row = fmin(cursor_row, 20);
    const char *current_line_content = p->selected_line->content;
    int col = fmin(p->selected_line->length, p->cursor_col);
    Vector2 cursor_pos = {60, 68};
    cursor_pos.x += MeasureTextEx(terminal_font, TextFormat("%.*s", col, current_line_content), 28, 1).x;
    cursor_pos.y += cursor_row * 28;
    DrawRectangleV(cursor_pos, (Vector2){font_size, 28}, WHITE);
    DrawTextEx(terminal_font, TextFormat("%c", current_line_content[col]), cursor_pos, 28, 1, BLACK);

    // Tooltip
    Vector2 tooltip_position = {60, 68 + 28 * 21};
    DrawTextEx(terminal_font, "[C-s] Save [C-q] Quit", tooltip_position, 28, 1, WHITE);

    if (p->tooltip_info) {
        int tooltip_info_length = MeasureTextEx(terminal_font, p->tooltip_info, 28, 1).x;
        Vector2 tooltip_info_position = {WIDTH - 60 - tooltip_info_length, 68 + 28 * 21};
        DrawTextEx(terminal_font, p->tooltip_info, tooltip_info_position, 28, 1, WHITE);
    }
}

int netscan_init(terminal *term, int argc, const char **argv) {
    (void)term;
    (void)argc;
    (void)argv;
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

int netscan_update(terminal *term, void *args) {
    netscan_process *p = (netscan_process *)args;
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_C)) {
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

int test_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;
    test_process *p = malloc(sizeof(*p));
    assert(p != NULL);
    term->args = p;
    return 0;
}

static void put_pixel(int *fb, int x, int y, int color) {
    if (x < 0 || x >= FB_SIZE_WIDTH || y < 0 || y >= FB_SIZE_HEIGHT)
        return;
    fb[y * FB_SIZE_WIDTH + x] = color;
}

static void draw_line(int *fb, int x0, int y0, int x1, int y1, int color) {
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    while (1) {
        put_pixel(fb, x0, y0, color);

        if (x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
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

int test_update(terminal *term, void *args) {
    test_process *p = (test_process *)args;
    term->title = NULL;
    memset(p->fb, 0, sizeof(*p->fb) * FB_SIZE);

    float x_min = -10.0f;
    float x_max = 10.0f;

    float x_scale = (float)FB_SIZE_WIDTH / (x_max - x_min);
    float y_scale = (float)FB_SIZE_HEIGHT / 2.0f;

    // Pas d’échantillonnage fin
    const float dx = 0.01f;

    int prev_x = 0;
    int prev_y = 0;
    int first = 1;

    for (float x = x_min; x <= x_max; x += dx) {
        float y = sinf(x / 5 + GetTime());

        int px = (int)((x - x_min) * x_scale);
        int py = (int)(FB_SIZE_HEIGHT / 2.f - y * y_scale);

        if (!first)
            draw_line(p->fb, prev_x, prev_y, px, py, TERM_RED);
        else
            first = 0;

        prev_x = px;
        prev_y = py;
    }

    return 0;
}

void test_render(terminal *term, void *args) {
    (void)term;
    test_process *p = (test_process *)args;
    render_framebuffer(p->fb);
}

static void put_pixel(int *fb, int x, int y, int color);

void put_pixel_fn(stmt_funcall *call) {
    (void)call;
    exec_process *p = (exec_process *)active_term->args;
    int c = basic_pop_value_num();
    int y = basic_pop_value_num();
    int x = basic_pop_value_num();
    put_pixel(p->fb[1 - p->fb_idx], x, y, c % TERM_COUNT);
    basic_push_function_result(0);
}

void flip_render_fn(stmt_funcall *call) {
    (void)call;
    exec_process *p = (exec_process *)active_term->args;
    p->fb_idx = 1 - p->fb_idx;
}

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
    p->interpreter = (basic_interpreter){.print_fn = terminal_basic_print, .append_print_fn = terminal_append_print};
    memset(p->fb[0], 0, sizeof(*p->fb[0]) * FB_SIZE);
    memset(p->fb[1], 0, sizeof(*p->fb[1]) * FB_SIZE);
    terminal_append_log(t, "");
    init_interpreter(&p->interpreter, file->content);
    register_function("PUTPIXEL", put_pixel_fn, 3);
    register_function("RENDER", flip_render_fn, 0);
    register_function("COLOR_RED", flip_render_fn, 0);
    register_variable_int("COLOR_BG", TERM_BG);
    register_variable_int("COLOR_FG", TERM_FG);
    register_variable_int("COLOR_BLUE", TERM_BLUE);
    register_variable_int("COLOR_GREEN", TERM_GREEN);
    register_variable_int("COLOR_RED", TERM_RED);
    register_variable_int("COLOR_YELLOW", TERM_YELLOW);
    register_variable_int("COLOR_PURPLE", TERM_PURPLE);
    t->args = p;
    return 0;
}

int exec_update(terminal *t, void *args) {
    exec_process *p = (exec_process *)args;
    t->title = NULL;
    if (IsKeyPressed(KEY_C) && IsKeyDown(KEY_LEFT_CONTROL)) {
        destroy_interpreter();
        return 1;
    }
    advance_interpreter_time(&p->interpreter, GetFrameTime());
    for (int i = 0; i < 100000; i++) {
        if (!step_program(&p->interpreter)) {
            t->log_count--;
            return 1;
        }
    }
    return 0;
}

void exec_render(terminal *t, void *args) {
    (void)t;
    exec_process *p = (exec_process *)args;
    render_framebuffer(p->fb[p->fb_idx]);
}

typedef struct {
    const char *short_subject;
    const char *full_subject;
    const char *from;
    const char *to;
    const char *content[8];
    const char *anwsers[3];
    int anwsers_count;
} mail;

// TODO: Load mail from external source
mail mails[3] = {
    {.short_subject = "Hello !",
     .full_subject = "How are you doing",
     .from = "abc@xxx.com (User 1)",
     .to = "xyz@xxx.com (You)",
     .content = {"Dear Man", "Hope you are doing well tonigh.", "Fuck you !!!", "Cheers."},
     .anwsers = {"Yes", "No", "Maybe"},
     .anwsers_count = 3},
    {.short_subject = "Good bye !",
     .full_subject = "See you soon !",
     .from = "abc@xxx.com (User 1)",
     .to = "xyz@xxx.com (You)",
     .content = {"Dear Man", "A la prochaine"},
     .anwsers = {"Yes", "No"},
     .anwsers_count = 2},
    {.short_subject = "Re",
     .full_subject = "Re",
     .from = "abc@xxx.com (User 1)",
     .to = "xyz@xxx.com (You)",
     .content = {"Dear Bro", "See you next time"},
     .anwsers = {"No"},
     .anwsers_count = 1},
};
int mail_count = 3;
int selected_mail_idx = 0;
int selected_anwser_idx = 0;

int mail_update(terminal *term, void *args) {
    term->title = "Mails";
    if (IsKeyPressed(KEY_A)) {
        return 1;
    }
    if (IsKeyPressed(KEY_DOWN)) {
        selected_mail_idx = fmin(mail_count - 1, selected_mail_idx + 1);
        selected_anwser_idx = 0;
    }
    if (IsKeyPressed(KEY_UP)) {
        selected_mail_idx = fmax(0, selected_mail_idx - 1);
        selected_anwser_idx = 0;
    }
    if (IsKeyPressed(KEY_LEFT)) {
        selected_anwser_idx--;
        if (selected_anwser_idx < 0)
            selected_anwser_idx = mails[selected_mail_idx].anwsers_count - 1;
    }
    if (IsKeyPressed(KEY_RIGHT)) {
        selected_anwser_idx = (selected_anwser_idx + 1) % mails[selected_mail_idx].anwsers_count;
    }

    (void)args;
    return 0;
}

void mail_render(terminal *term, void *args) {
    // Layout
    const int split_position = WIDTH * 0.35;
    DrawRectangle(split_position, 0, 15, HEIGHT, WHITE);
    DrawRectangle(split_position, HEIGHT * 0.25, WIDTH, 8, WHITE);

    // Mailbox
    for (int i = 0; i < mail_count; i++) {
        const mail *m = &mails[mail_count - i - 1];
        if (i == selected_mail_idx) {
            DrawRectangle(0, 60 + 60 * i, split_position, 60, WHITE);
        } else {
            DrawRectangle(0, 120 + 60 * i, split_position, 8, WHITE);
        }
        DrawTextEx(terminal_font, m->short_subject, (Vector2){60, 80 + 60 * i}, font_size, 1,
                   i == selected_mail_idx ? BLACK : WHITE);
    }

    mail *m = &mails[selected_mail_idx];
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

    // Anwsers
    int width = (WIDTH - split_position - 75) / m->anwsers_count;
    for (int i = 0; i < m->anwsers_count; i++) {
        int x = split_position + 25 + width * i;
        const char *txt = m->anwsers[i];
        int middle = (width - MeasureTextEx(terminal_font, txt, 36, 1).x) / 2;
        Color txt_color = i == selected_anwser_idx ? BLACK : WHITE;

        if (i == selected_anwser_idx) {
            DrawRectangle(x, HEIGHT - 100, width, 50, WHITE);
        } else {
            DrawRectangleLinesEx((Rectangle){x, HEIGHT - 100, width, 50}, 3, WHITE);
        }
        DrawTextEx(terminal_font, txt, (Vector2){x + middle, HEIGHT - 90}, 36, 1, txt_color);
    }

    (void)term;
    (void)args;
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
        const char *str = TextFormat("f    %-16s %-4d bytes", root->name, strlen(root->content));
        terminal_append_log(term, str);
        return 0;
    }

    for (int i = 0; i < root->children_count; i++) {
        file_node *node = root->children[i];
        if (node->folder) {
            const char *str = TextFormat("d    %-16s %-4d children", node->name, node->children_count);
            terminal_append_log(term, str);
        } else {
            const char *str = TextFormat("f    %-16s %-4d bytes", node->name, strlen(node->content));
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
        if (node->content != NULL) {
            const char *line = node->content;
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
        file_node_append_children(term->fs.pwd, file_new(argv[2], ""));
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
        terminal_append_log(term, TextFormat("Machine %s online", ip));
    } else {
        terminal_append_log(term, TextFormat("Machine %s offline", ip));
    }
    return 0;
}

int hostname_init(terminal *term, int argc, const char **argv) {
    (void)argc;
    (void)argv;
    terminal_append_log(term, term->hostname);
    return 0;
}

int mail_init(terminal *term, int argc, const char **argv) {
    (void)term;
    (void)argc;
    (void)argv;
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
    for (int i = 0; i < term->log_count; i++) {
        free((void *)term->logs[i]);
    }
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
    term->process_update = &ping_update;
    ping_process *p = malloc(sizeof(ping_process));
    assert(p != NULL);
    p->remaning_count = 4;
    p->remaining_time = 1;
    p->ip = strdup(ip);
    term->args = p;
    return 0;
}

int help_init(terminal *term, int argc, const char **argv) {
    int default_selected = 0;
    if (argc >= 2) {
        const char *asked_command = argv[1];
        default_selected = get_command_id(asked_command);
        if (default_selected == -1) {
            terminal_append_log(term, TextFormat("Unknown command : %s", asked_command));
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

int help_update(terminal *term, void *args) {
    if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_A)) {
        return 1;
    }
    help_process *p = (help_process *)args;
    term->title = TextFormat("Help : %s", commands[p->selected_index].name);
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
        p->selected_index = fmin(p->selected_index + 1, command_count - 1);
        if (p->selected_index - p->offset >= 10) {
            p->offset = fmin(p->offset + 1, command_count - 10);
        }
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
        p->selected_index = fmax(p->selected_index - 1, 0);
        if (p->selected_index == p->offset - 1) {
            p->offset = fmax(p->offset - 1, 0);
        }
    }
    return 0;
}

void help_render(terminal *term, void *args) {
    (void)term;
    const help_process *p = (help_process *)args;

    // Layout
    const int split_position = WIDTH * 0.35;
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

// TODO: Remove empty space argv
void terminal_handle_command(terminal *term) {
    term->history_ptr = 0;
    int argc = 0;
    const char *input = TextFormat("%.*s", term->input_cursor, term->input);
    const char **argv = TextSplit(input, ' ', &argc);
    terminal_append_input(term);
    if (argc == 0 || *argv[0] == '\0') {
        return;
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
        if (c->init(term, argc, argv) != 0) {
            return;
        }
        if (c->update != NULL) {
            term->process_update = c->update;
        }
        if (c->render != NULL) {
            term->process_render = c->render;
        }
    } else {
        terminal_append_log(term, TextFormat("Unknown command : %s", argv[0]));
    }
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

// TODO: Cycling on TAB
const char *terminal_autocomplete_file(file_node *root, const char *path) {
    file_node *node = get_parent_from_path(root, path);
    if (node == NULL) {
        return NULL;
    }
    const char *input_filename = get_path_filename(path);
    size_t path_length = strlen(input_filename);
    const char *completed_file_path = NULL;
    for (int i = 0; i < node->children_count; i++) {
        const char *file_name = node->children[i]->name;
        if (path_length <= strlen(file_name)) {
            if (strncmp(input_filename, file_name, path_length) == 0) {
                completed_file_path = file_name + path_length;
                break;
            }
        }
    }
    return completed_file_path;
}

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

typedef struct {
    char **items;
    int count;
    int capacity;
} text_lines;

text_lines text_split(char *content, char sep) {
    text_lines result = {0};

    if (content == NULL) {
        return result;
    }

    char *s = content;
    append(&result, s);
    while (*s) {
        if (*s == sep) {
            *s = '\0';
            s++;
            append(&result, s);
        } else {
            s++;
        }
    }
    return result;
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
                    file_node_append_file_full_path(all_terminals[machine_idx].fs.root, file, content_with_new_lines);
                    free(content_with_new_lines);
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

typedef struct {
    const char *content;
    int waiting_time;
    bool override_previous;
    void (*callback)(void);
} bootup_sequence_line;

bootup_sequence_line bootup_sequence[] = {
    {"NyxLoader Boot Loader v1.4", 500, false, &bootseq_beep},
    {"Loading Kernel", 500, false, NULL},
    {"Loading Kernel .", 500, true, NULL},
    {"Loading Kernel ..", 1000, true, NULL},
    {"Loading Kernel ...", 500, true, NULL},
    {"Loading Kernel ... OK", 200, true, NULL},
    {"Passing control to kernel", 200, false, NULL},
    {"", 600, false, NULL},
    {"HemeraKernel v2.4.1", 100, false, NULL},
    {"Copyright (c) 1987 HemeraLabs", 100, false, NULL},
    {"", 600, false, NULL},
    {"CPU: h512X3 @ 66MHz", 100, false, NULL},
    {"Memory: 12MB detected", 100, false, NULL},
    {"", 600, false, NULL},
    {"Probing system components", 1000, false, NULL},
    {"Probing system components .", 1000, true, NULL},
    {"Probing system components ..", 1000, true, NULL},
    {"Probing system components ...", 1000, true, NULL},
    {"ART bus detected", 100, false, NULL},
    {"PRC bus detected", 100, false, NULL},
    {"", 600, false, NULL},
    {"Mounting root filesystem .", 300, false, NULL},
    {"Mounting root filesystem ..", 300, true, NULL},
    {"Mounting root filesystem ...", 300, true, NULL},
    {"Root mounted from AO0 at /", 300, false, NULL},
    {"", 600, false, NULL},
    {"Bringing up network interfaces .", 300, false, NULL},
    {"Bringing up network interfaces ..", 300, true, NULL},
    {"Bringing up network interfaces ...", 300, true, NULL},
    {"Available network interfaces:", 300, false, NULL},
    {"lo0: loopback configured", 300, false, NULL},
    {"eth0: address 00:40:12:3A:9F:2C", 300, false, NULL},
    {"", 600, false, NULL},
    {"Getting IP address for eth0", 1000, false, NULL},
    {"eth0: 192.168.1.96", 300, false, NULL},
    {"", 600, false, NULL},
    {"Network ready.", 300, false, NULL},
    {"Starting up user environment .", 300, false, NULL},
    {"Starting up user environment ..", 300, true, NULL},
    {"Starting up user environment ...", 300, true, NULL},
    {"Starting up user environment ... OK", 300, true, NULL},
    {"", 600, false, NULL},
    {"Welcome to HemeraOS v1.0.2 (Ciros)", 4000, false, NULL},
};
const int bootup_sequence_count = sizeof(bootup_sequence) / sizeof(bootup_sequence[0]);

int bootup_sequence_idx = -1;
float bootup_sequence_line_time_left = 1000;

#define DISABLE_BOOTUP_SEQUENCE 1

int bootup_sequence_update(terminal *term, void *args) {
#if DISABLE_BOOTUP_SEQUENCE
    return 1;
#endif
    (void)args;
    term->title = NULL;
    bootup_sequence_line_time_left -= GetFrameTime() * 1000;
    if (1) {
        if (bootup_sequence_line_time_left < 0) {
            bootup_sequence_idx++;
            if (bootup_sequence_idx == bootup_sequence_count) {
                return 1;
            }
            bootup_sequence_line_time_left = bootup_sequence[bootup_sequence_idx].waiting_time;
            if (bootup_sequence[bootup_sequence_idx].callback) {
                bootup_sequence[bootup_sequence_idx].callback();
            }
        }
    } else {
        if (IsKeyPressed(KEY_N)) {
            bootup_sequence_idx++;
            if (bootup_sequence_idx == bootup_sequence_count) {
                return 1;
            }
            bootup_sequence_line_time_left = bootup_sequence[bootup_sequence_idx].waiting_time;
            if (bootup_sequence[bootup_sequence_idx].callback) {
                bootup_sequence[bootup_sequence_idx].callback();
            }
        }
    }
    return 0;
}

void bootup_sequence_render(terminal *term, void *args) {
    (void)term;
    (void)args;

    int real_line_count = 0;
    for (int i = 0; i <= bootup_sequence_idx; i++) {
        if (bootup_sequence[i].override_previous == false) {
            real_line_count++;
        }
    }

    int line = -1;
    int start_line = fmax(0, real_line_count - MAX_LINE_COUNT_PER_SCREEN);

    int real_start = 0;
    int current_line = 0;
    for (int i = 0; i < bootup_sequence_idx; i++) {
        if (bootup_sequence[i].override_previous == false) {
            current_line++;
        }
        if (current_line == start_line) {
            real_start = i;
            break;
        }
    }

    for (int i = real_start; i <= bootup_sequence_idx; i++) {
        if (bootup_sequence[i].override_previous == false) {
            line++;
        }
        DrawTerminalLine(bootup_sequence[i].content, line - 1);
    }

    if (bootup_sequence_idx >= 0) {
        if ((int)GetTime() % 2 == 0) {
            int x_offset = bootup_sequence[bootup_sequence_idx].content[0] == '\0' ? 60 : 68;
            int x =
                x_offset + MeasureTextEx(terminal_font, bootup_sequence[bootup_sequence_idx].content, font_size, 1).x;
            int y = 40 + font_size * line;
            DrawRectangle(x, y, 10, font_size, WHITE);
        }
    }
}

int main() {
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(WIDTH, HEIGHT, "TUIGame");
    InitAudioDevice();
    SetTargetFPS(60);

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
    bootseq_init();

    while (!WindowShouldClose()) {
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
            if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && active_term->input_cursor > 0) {
                active_term->input_cursor--;
            }
            if (IsKeyPressed(KEY_ENTER)) {
                terminal_handle_command(active_term);
            }
            if (IsKeyPressed(KEY_UP)) {
                int offset = active_term->history_count - active_term->history_ptr - 1;
                if (offset >= 0) {
                    strncpy(active_term->input, active_term->history[offset], MAX_INPUT_LENGTH);
                    active_term->input_cursor = strlen(active_term->input);
                    active_term->history_ptr++;
                }
            }
            if (IsKeyPressed(KEY_DOWN)) {
                if (active_term->history_ptr > 1) {
                    active_term->history_ptr--;
                    int offset = active_term->history_count - active_term->history_ptr;
                    strncpy(active_term->input, active_term->history[offset], MAX_INPUT_LENGTH);
                    active_term->input_cursor = strlen(active_term->input);
                } else {
                    active_term->history_ptr = 0;
                    active_term->input_cursor = 0;
                }
            }
            if (IsKeyPressed(KEY_TAB)) {
                terminal_autocomplete_input(active_term);
            }
            if (IsKeyDown(KEY_LEFT_CONTROL) && IsKeyPressed(KEY_L)) {
                clear_init(active_term, 0, NULL);
            }
        }

        if (active_term->process_update != NULL) {
            if (active_term->process_update(active_term, active_term->args)) {
                active_term->process_update = NULL;
                active_term->process_render = NULL;
                free(active_term->args);
                active_term->args = NULL;
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

            if (active_term->process_render == NULL) {
                terminal_render(active_term);
            } else {
                active_term->process_render(active_term, active_term->args);
            }
            if (active_term->process_update == NULL) {
                terminal_render_prompt(active_term);
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
    CloseWindow();
}
