#ifndef _LIB_INTERRUPT_H
#define _LIB_INTERRUPT_H
#include "stdint.h"
typedef void* intr_handler;
void idt_init(void);
void register_handler(uint8_t vector_no,intr_handler function);

/*INTR_ON表示中断打开，INTR_OFF表示中断关闭*/
enum intr_status {
	INTR_ON,
	INTR_OFF
};

enum intr_status intr_get_status(void);
enum intr_status intr_set_status(enum intr_status);
enum intr_status intr_disable(void);
enum intr_status intr_enable(void);

#endif
