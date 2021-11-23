#include "timer.h"
#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "thread.h"
#include "debug.h"
#include "global.h"

#define IRQ0_FREQUENCY 100	//每秒产生的时钟频率
#define INPUT_FREQUENCY 1193180 //计数器每秒产生的时钟脉冲
#define COUNTER0_VALUE INPUT_FREQUENCY / IRQ0_FREQUENCY
#define COUNTER0_PORT 0x40
#define COUNTER0_NO 0 //代表计数器0
#define COUNTER0_MODE 2 //第二种工作方式
#define READ_WRITE_LATCH 3 //读写16位
#define PIT_CONTROL_PORT 0x43 //读写控制器的端口
#define mil_seconds_per_intr (1000/IRQ0_FREQUENCY)//per intr use how much mil_seconds

uint32_t ticks;

static void ticks_to_sleep(uint32_t sleep_ticks) {
	uint32_t start_ticks = ticks;

	while (ticks - start_ticks < sleep_ticks)
	{
		thread_yield();
	}
}

void mtime_sleep(uint32_t m_seconds) {
	//calculate the count of interrupt
	//in other words, the caller want to stop cur_thread sleep_ticks times interrupt 
	uint32_t sleep_ticks = DIV_ROUND_UP(m_seconds,mil_seconds_per_intr);
	ASSERT(sleep_ticks > 0);
	ticks_to_sleep(sleep_ticks);
}



static void frequency_set(uint8_t counter_port, uint8_t counter_no,
			  uint8_t rwl, uint8_t counter_mode,
			  uint16_t counter_value){
	
	//向寄存器0x43中写入控制字
	outb(PIT_CONTROL_PORT,(uint8_t) (counter_no << 6 | rwl << 4 | counter_mode << 1));
	//在写入counter_value的底8字
	outb(counter_port,(uint8_t) counter_value);
	//高8字节
	put_str("(uint8_t)counter_value >> 8 is : ");put_int((uint8_t)counter_value >> 8);put_char('\n');
	put_str("(uint8_t)(counter_value >> 8) is : ");put_int((uint8_t)(counter_value >> 8));put_char('\n');
	outb(counter_port,(uint8_t) (counter_value >> 8));
}

static void intr_timer_handler(void) {
	struct task_struct* cur = running_thread();
	ASSERT(cur->stack_magic == 0x12345678);
	cur->elapsed_ticks ++;
	ticks ++;
	if(cur->ticks == 0) {
		schedule();
	} else{
		cur->ticks --;
	}
}

void timer_init(void){
	put_str("timer_init start\n");
	frequency_set(COUNTER0_PORT,COUNTER0_NO,READ_WRITE_LATCH,COUNTER0_MODE,COUNTER0_VALUE);
	register_handler(0x20,intr_timer_handler);
	put_str("timer_init done\n");
}
