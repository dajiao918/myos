#ifndef THREAD_SYNC_H
#define THREAD_SYNC_H

#include "stdint.h"
#include "list.h"
#include "thread.h"

struct semaphore{
	uint8_t value;
	struct list waiters;//阻塞队列
};

struct lock{
	struct task_struct* holder;
	struct semaphore semaphore;
	uint32_t holder_repeat_nr;
};

void lock_init(struct lock* lock);
void lock_acquire(struct lock* plock);
void lock_release(struct lock* plock);
void sema_init(struct semaphore* sema, uint8_t value);
void sema_down(struct semaphore* psema);
void sema_up(struct semaphore* psema);
#endif

