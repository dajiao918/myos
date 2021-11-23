#ifndef _USERPROG_TSS_H
#define _USERPROG_TSS_H
#include "thread.h"

void tss_init(void);
void update_tss_esp(struct task_struct* pthread);

#endif
