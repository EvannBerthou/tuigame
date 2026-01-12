#ifndef BASIC_H
#define BASIC_H

typedef struct basic_interpreter basic_interpreter;

bool interpreter_init(const char *src, void (*print_fn)(const char *), void (*append_fn)(const char *));
void advance_interpreter_time(float time);
bool step_program();
void destroy_interpreter();

void register_function(const char *name, void (*f)(), int arg_count);
void register_variable_int(const char *name, int value);
void register_variable_string(const char *name, const char *value);

void basic_push_int(int result);
void basic_push_string(const char *s);
int basic_pop_value_num();
const char *basic_pop_value_string();
void basic_sleep(float seconds);

#endif
