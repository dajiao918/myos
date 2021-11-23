#include "stdint.h"
#include "print.h"
#include "thread.h"
#include "list.h"
#include "sync.h"
#include "debug.h"
#include "stdio-kernel.h"

void sema_init(struct semaphore* psema, uint8_t value){
	psema->value = value;
	list_init(&psema->waiters);
}

void lock_init(struct lock* lock){
	lock->holder = NULL;
	lock->holder_repeat_nr = 0;
	sema_init(&lock->semaphore,1);
}

void sema_down(struct semaphore* psema){
	enum intr_status old_status = intr_disable();
	while(psema->value == 0) {
		
		ASSERT(!elem_find(&psema->waiters,&running_thread()->general_tag));
		if(elem_find(&psema->waiters,&running_thread()->general_tag)) {
			PANIC("sema_down: thread_blocked has already in waiters list");
		}
		list_append(&psema->waiters,&running_thread()->general_tag);
		thread_block(TASK_BLOCKED);
	}
	psema->value --;
	ASSERT(psema->value == 0);
	intr_set_status(old_status);
}

void sema_up(struct semaphore* psema) {
	enum intr_status old_status = intr_disable();
	ASSERT(psema->value == 0);
	if(!list_empty(&psema->waiters)) {
		struct task_struct* thread_blocked = elem2entry(struct task_struct,general_tag,list_pop(&psema->waiters));
		
		thread_unblock(thread_blocked);
	}
	psema->value ++;
	ASSERT(psema->value == 1);
	intr_set_status(old_status);
	//asm volatile("int $0x20");
}

void lock_acquire(struct lock* plock){
	
	if(plock->holder != running_thread()){
		sema_down(&plock->semaphore);
		plock->holder = running_thread();
		ASSERT(plock->holder_repeat_nr == 0);
		plock->holder_repeat_nr = 1;
	} else {
		plock->holder_repeat_nr ++;
	}
}

void lock_release(struct lock* plock) {
	ASSERT(plock->holder == running_thread());
	if(plock->holder_repeat_nr > 1) {
		plock->holder_repeat_nr --;
		return;
	}
	ASSERT(plock->holder_repeat_nr == 1);
	plock->holder_repeat_nr = 0;
	plock->holder = NULL;
	sema_up(&plock->semaphore);
}

