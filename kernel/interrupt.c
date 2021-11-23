#include "interrupt.h"
#include "stdint.h"
#include "global.h"
#include "io.h"
#include "print.h"
#include "debug.h"
#include "thread.h"

#define IDT_DESC_CNT 0x81 //目前支持的终端数量
#define PIC_M_CTRL 0x20
#define PIC_M_DATA 0x21
#define PIC_S_CTRL 0xa0
#define PIC_S_DATA 0xa1

#define EFLAGS_IF 0x00000200 //eflags寄存器的if位
//定义宏来获取eflags的值，g表示任意内存或者寄存器
#define GET_EFLAGS(EFLAG_VAR) asm volatile("pushfl; popl %0":"=g"(EFLAG_VAR))

//中断描述符的结构体
struct gate_desc{
	uint16_t func_offset_low_word; //中断程序的偏移低16字节
	uint16_t selector;	//选择子
	uint8_t dcount;	
	uint8_t attribute; 	//系统段，type=1110
	uint16_t func_offset_high_word;
};

static void make_idt_desc(struct gate_desc* p_gdesc, uint8_t attr, intr_handler function);

//中断描述符数组，初始化，加载进idt
static struct gate_desc idt[IDT_DESC_CNT];

//中断处理函数的地址
intr_handler idt_table[IDT_DESC_CNT];

//中断的信息
char* intr_name[IDT_DESC_CNT];

//定义在kernel.s的intr_entry_table
extern intr_handler intr_entry_table[IDT_DESC_CNT];
extern void set_cursor(uint32_t cursor_pos);
//系统调用入口
extern uint32_t syscall_handler(void);

static void pic_init(void){
	
	outb(PIC_M_CTRL,0x11); //ICW1，边沿触发，级联，需要ICW4
	outb(PIC_M_DATA,0x20); //ICW2,中断号从0x20开始
	outb(PIC_M_DATA,0x04); //从片和主片的IR2级联
	outb(PIC_M_DATA,0x01); //8086模式，正常EOI
	
	outb(PIC_S_CTRL,0x11);
	outb(PIC_S_DATA,0x28);
	outb(PIC_S_DATA,0x02);
	outb(PIC_S_DATA,0x01);
	
	outb(PIC_M_DATA,0xf8);//写MBR，打开IR0(0x20)，IR1(0x21)，IR2(接受从片的中断),
	outb(PIC_S_DATA,0xbf);//IR14,键盘中断
	
	put_str("pic_init done!\n");
}

static void make_idt_desc(struct gate_desc* idtDesc, uint8_t attr, intr_handler function){
	idtDesc->func_offset_low_word = (uint32_t)function & 0x0000FFFF;
	idtDesc->selector = SELECTOR_K_CODE;
	idtDesc->dcount = 0;
	idtDesc->attribute = attr;
	idtDesc->func_offset_high_word = ((uint32_t)function & 0xFFFF0000) >> 16;
}

static void idt_desc_init(void){
	int i, last_index = IDT_DESC_CNT - 1;
	for(i = 0; i < IDT_DESC_CNT; i ++) {
		make_idt_desc(&idt[i],IDT_DESC_ATTR_DPL0,intr_entry_table[i]);
	}
	make_idt_desc(&idt[last_index],IDT_DESC_ATTR_DPL3,syscall_handler);
	put_str("idt_desc_init done!\n");
}

static void general_intr_handler(uint8_t vec_nr) {
	if(vec_nr == 0x27 || vec_nr == 0x2f){
		return;
	}
	set_cursor(0);
	int cursor_pos = 0;
	while(cursor_pos < 320) {
		put_char(' ');
		cursor_pos ++;
	}
	set_cursor(0);
	put_str("!!!!! exception occur !!!!!");
	set_cursor(86);
	put_str(intr_name[vec_nr]);
	if(vec_nr == 14) {
		int page_fault_addr = 0;
		asm ("movl %%cr2,%0":"=r"(page_fault_addr));
		put_str("\npage fault addr is: ");put_int(page_fault_addr);
	}
	put_str("    cur_thread is ");
	put_str(running_thread()->name);
	put_str("\n !!!!! exception end !!!!!");
	while(1);
}

void register_handler(uint8_t vector_no, intr_handler function) {
	idt_table[vector_no] = function;
}

static void exception_init(void){
	int i;
	for(i = 0; i < IDT_DESC_CNT; i++){
		idt_table[i] = general_intr_handler;
		intr_name[i] = "unKnow";
	}

	intr_name[0] = "#DE Divide ERROR!";
	intr_name[1] = "#DB Debug Exception!";
	intr_name[2] = "NMI Interrupt!";
	intr_name[3] = "#BP BreakPiont Exception!";
	intr_name[4] = "#OF Overflow Exception!";
	intr_name[5] = "#BR bound range exceeded Exception!";
	intr_name[6] = "#UD Invalid Opcode Eception!";
	intr_name[7] = "#NM device not available exception!";
	intr_name[8] = "DF Double Fault Exception!";
	intr_name[9] = "Coprocessor segment overreturn!";
	intr_name[10] = "#TS Invalid Tss Exception!";
	intr_name[11] = "#NP Segment Not Present!";
	intr_name[12] = "#SS Stack Fault Exception!";
	intr_name[13] = "#GP General protection exception!";
	intr_name[14] = "#PG page fault exception!";
	intr_name[16] = "#Mf x87 FPU Floating-point error!";
	intr_name[17] = "#AC alignment Check Exception!";
	intr_name[18] = "#MC Mahcine-check exception!";
	intr_name[19] = "#Xf SIMD Floating-Point Exception!";
}

/*打开中断*/
enum intr_status intr_enable(){
	enum intr_status old_status;
	if(INTR_ON == intr_get_status()){
		old_status = INTR_ON;
		return old_status;
	} else {
		old_status = INTR_OFF;
		asm volatile("sti");
		return old_status;
	}
}

/*将中断关闭*/
enum intr_status intr_disable(){
	enum intr_status old_status;
	if(INTR_ON == intr_get_status()){
		
		old_status = INTR_ON;
		asm volatile("cli": : :"memory");
		return old_status;
	} else {
		old_status = INTR_OFF;
		return old_status;
	}
}

/*将中断设置为status*/
enum intr_status intr_set_status(enum intr_status status) {

	if(status == INTR_ON) {
		return intr_enable();
	} else {
		return intr_disable();
	}	
	//return (status & INTR_ON) ? intr_enable() : intr_disable();
}

/*获取中断是否打开*/
enum intr_status intr_get_status(){
	uint32_t eflags = 0;
	GET_EFLAGS(eflags);
	//相与为1代表中断打开了，否则中断没有开
	return (eflags & EFLAGS_IF) ? INTR_ON : INTR_OFF;
}

void idt_init(){
	
	put_str("idt_init start!\n");
	idt_desc_init();
	exception_init();
	pic_init();
	
	uint64_t idt_operand = (sizeof(idt) - 1) | (((uint64_t)(uint32_t)idt) << 16);
	asm volatile("lidt %0": : "m" (idt_operand));
	put_str("idt_init done!\n");
}

