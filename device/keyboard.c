#include "io.h"
#include "print.h"
#include "interrupt.h"
#include "keyboard.h"
#include "global.h"
#include "stdint.h"
#include "ioqueue.h"

#define KBD_BUF_PORT 0x60//kerboard register port

#define esc '\033'
#define backspace '\b'
#define tab '\t'
#define enter '\r'
#define delete '\177'

#define char_invisable 0

#define ctrl_l_char char_invisable
#define ctrl_r_char char_invisable
#define shift_l_char char_invisable
#define shift_r_char char_invisable
#define alt_l_char char_invisable
#define alt_l_char char_invisable
#define caps_lock_char char_invisable

#define shift_l_make 0x2a
#define shift_r_make 0x36
#define alt_l_make 0x38
#define alt_r_make 0xe038
#define alt_r_break 0xe0b8
#define ctrl_l_make 0x1d
#define ctrl_r_make 0xe01d
#define ctrl_r_break 0xe09d
#define caps_lock_make 0x3a

struct ioqueue kbd_buf;

static char keymap[][2] = {
/*0x00*/	{0,0},
/*0x01*/	{esc,esc},
/*0x02*/	{'1','!'},
/*0x03*/	{'2','@'},
/*0x04*/	{'3','#'},
/*0x05*/	{'4','$'},
/*0x06*/	{'5','%'},
/*0x07*/	{'6','^'},
/*0x08*/	{'7','&'},
/*0x09*/	{'8','*'},
/*0x0a*/	{'9','('},
/*0x0b*/	{'0',')'},
/*0x0c*/	{'-','_'},
/*0x0d*/	{'=','+'},
/*0x0e*/	{backspace,backspace},
/*0x0f*/	{tab,tab},
/*0x10*/	{'q','Q'},
/*0x11*/	{'w','W'},
/*0x12*/	{'e','E'},
/*0x13*/	{'r','R'},
/*0x14*/	{'t','T'},
/*0x15*/	{'y','Y'},
/*0x16*/	{'u','U'},
/*0x17*/	{'i','I'},
/*0x18*/	{'o','O'},
/*0x19*/	{'p','P'},
/*0x1a*/	{'[','{'},
/*0x1b*/	{']','}'},
/*0x1c*/	{enter,enter},
/*0x1d*/	{ctrl_l_char,ctrl_l_char},
/*0x1e*/	{'a','A'},
/*0x1f*/	{'s','S'},
/*0x20*/	{'d','D'},
/*0x21*/	{'f','F'},
/*0x22*/	{'g','G'},
/*0x23*/	{'h','H'},
/*0x24*/	{'j','J'},
/*0x25*/	{'k','K'},
/*0x26*/	{'l','L'},
/*0x27*/	{';',':'},
/*0x28*/	{'\'','"'},
/*0x29*/	{'`','~'},
/*0x2a*/	{shift_l_char,shift_l_char},
/*0x2b*/	{'\\','|'},
/*0x2c*/	{'z','Z'},
/*0x2d*/	{'x','X'},
/*0x2e*/	{'c','C'},
/*0x2f*/	{'v','V'},
/*0x30*/	{'b','B'},
/*0x31*/	{'n','N'},
/*0x32*/	{'m','M'},
/*0x33*/	{',','<'},
/*0x34*/	{'.','>'},
/*0x35*/	{'/','?'},
/*0x36*/	{shift_r_char,shift_r_char},
/*0x37*/	{'*','*'},
/*0x38*/	{alt_l_char,alt_l_char},
/*0x39*/	{' ',' '},
/*0x3a*/	{caps_lock_char,caps_lock_char}
};

static int ctrl_status, shift_status, alt_status, caps_lock_status = 0, \
		   ext_scancode;

static void intr_keyboard_handler(void) {
	//int ctrl_down_last = ctrl_status;
	//int caps_lock_last = caps_lock__status;
	//int shift_down_last = shift_status;
	
	int break_code;
	uint16_t scancode = inb(KBD_BUF_PORT);
	
	if(scancode == 0xe0){
		ext_scancode = 1;
		return;
	}

	if(ext_scancode) {
		scancode = ( 0xe000 | scancode );
		ext_scancode = 0;
	}

	break_code = (scancode & 0x0080);
	
	if(break_code) {
		uint16_t make_code = (scancode & 0xff7f);

		if(make_code == ctrl_l_make || make_code == ctrl_r_make) {
			ctrl_status = 0;
		} else if(make_code == alt_l_make || make_code == alt_r_make) {
			alt_status = 0;
		} else if(make_code ==shift_l_make || make_code == shift_r_make)		{
			shift_status = 0;
		}
		return;
	} else if( (scancode > 0x00 && scancode < 0x3b) || (scancode == alt_r_make) || (scancode == ctrl_r_make) ){
		
		int shift = 0;

		if((scancode < 0x0e) || (scancode == 0x29) || (scancode == 0x1a) || (scancode == 0x1b) || (scancode == 0x2b) || (scancode == 0x27) || (scancode == 0x28) || (scancode == 0x33) || (scancode == 0x34) || (scancode == 0x35)) 
		{
			if(shift_status) {
				shift = 1;
			}	
		} else {
			if(shift_status && caps_lock_status) {
				shift = 0;
			} else if(shift_status || caps_lock_status) {
				shift = 1;
			} else {
				shift = 0;
			}	
		}
		//获取数组中的下标
		uint8_t index = (scancode & 0x00ff);

		char input_char = keymap[index][shift];
		if(input_char) {
			if(!ioq_full(&kbd_buf)) {
				// put_char(input_char);
				ioq_putchar(&kbd_buf,input_char);
			}
			return;
		}

		if(scancode == shift_l_make || scancode == shift_r_make) {
			shift_status = 1;
		} else if(scancode == ctrl_l_make || scancode == ctrl_r_make) {
			ctrl_status = 1;
		} else if(scancode == alt_l_make || scancode == alt_r_make) {
			alt_status = 1;
		} else if(scancode == caps_lock_make) {
			caps_lock_status = !caps_lock_status;
		}
	} else {
		put_str("unknow key\n");
	}

}

void keyboard_init(){
	put_str("keyboard_init start\n");
	ioqueue_init(&kbd_buf);
	register_handler(0x21,intr_keyboard_handler);
	put_str("keyboard_init done\n");
}

