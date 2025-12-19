#ifndef BASIC_H
#define BASIC_H

typedef struct {
    void (*print_fn)(const char *text);
} basic_interpreter;

void execute_program(basic_interpreter *i, const char *src);

#endif
