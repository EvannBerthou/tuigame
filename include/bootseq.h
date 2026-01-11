#ifndef BOOTSEQ_H
#define BOOTSEQ_H

#define DISABLE_BOOTUP_SEQUENCE 1

typedef struct {
    const char *content;
    int waiting_time;
    bool override_previous;
    void (*callback)(void);
} bootup_sequence_line;

void bootseq_init();
void bootseq_beep();
bootup_sequence_line *bootseq_get_line(int i);

#endif
