%include "boot.inc"
section loader vstart=LOADER_BASE_ADDR
LOADER_STACK_TOP equ LOADER_BASE_ADDR
		jmp loader_start
	
;======构建gdt描述符表=========
GDT_BASE:	dd 0x00000000
	 	dd 0x00000000
CODE_DESC:	dd 0x0000FFFF
	   	dd DESC_CODE_HIGH4
DATA_STACK_DESC:dd 0x0000FFFF
	   	dd DESC_DATA_HIGH4
VIDEO_DESC:	dd 0x80000007
		dd DESC_VIDEO_HIGH4
	
GDT_SIZE equ $-GDT_BASE
GDT_LIMIT equ GDT_SIZE-1

		times 60 dq 0
;========配置段选择子========= 	
CODE_SELECTOR equ (0x0001<<3) + TI_GDT + RPL0
DATA_SELECTOR equ (0x0002<<3) + TI_GDT + RPL0
VIDEO_SELECTOR equ (0x0003<<3) + TI_GDT + RPL0

;=======方便gdtr寄存器进行load===
gdt_ptr 	dw GDT_LIMIT
		dd GDT_BASE

loadermsg 	db '2 loader in real'
			db 0

loader_start:	
;====利用int10中断打印字符串====
;AH功能号
;BH页码
;BL属性（若AL=00H或01H）
;CX=字符串长度
;(DH,DL)为坐标行、列
;ES:BP=字符串地址
;AL=显示输出方式
		mov sp, LOADER_BASE_ADDR
		mov bp,loadermsg ;字符串地址
		mov cx,17	;字符串长度
		mov ax,0x1301	;功能号以及01h
		mov bx,0x001f	;页号为0，蓝底分红字
		mov dx,0x1800	
		int 0x10
		
;======准备进入保护模式======
		;1.打开A20
		in al,0x92
		or al,0000_0010B
		out 0x92,al
		
		;2.加载GDT	
		lgdt [gdt_ptr]
	
		;3.cr0第0位置0，进入保护模式
		mov eax,cr0
		or eax,0x00000001
		mov cr0,eax
	 
	;刷新流水线
	;1.由于流水线的原因，下方的指令已经开始进行译码，而现在的段描述符缓冲寄存器的D字段还是位0，代表操作数位16为模式，但下面的代码是32位模式
	;2.段描述符高速缓冲寄存器没有更新，由于实模式也是用的这个寄存器，在保护模式下就不一定正确，所以需要更新流水线
		jmp dword CODE_SELECTOR:p_mode_start

[bits 32]
p_mode_start:
	
		mov ax,DATA_SELECTOR
		mov ds,ax
		mov es,ax
		mov ss,ax
		mov esp,LOADER_STACK_TOP
		mov ax,VIDEO_SELECTOR
		mov gs,ax
		
		mov eax,KERNEL_START_SECTOR
		mov ebx,KERNEL_BIN_BASE_ADDR
		mov ecx,200
		
	.load_kernel:
		call read_disk_m_32
		inc eax
		loop .load_kernel
				
		call setup_page
		sgdt [gdt_ptr]
		
		mov ebx,[gdt_ptr+2]
		;更新视频段的基地址，由于是平坦模型，代码段和数据段的基地址都是0，不用更新
		or dword [ebx+0x18+0x4],0xc0000000
		;修改gdt的基地址
		add dword [gdt_ptr+2],0xc0000000
		add esp,0xc0000000			;更新站指针
		;将页目录基地址加载到cr3中
		mov eax,PAGE_DIR_TABLE_POS
		mov cr3,eax
		
		mov eax,cr0
		or eax,0x80000000
		mov cr0,eax
		
		lgdt [gdt_ptr]
		
		jmp CODE_SELECTOR:enter_kernel
		
	enter_kernel:	
		mov esi,KERNEL_BIN_BASE_ADDR
		call kernel_init
		mov esp,0xc009f000
		mov byte [gs:160], 'V'
		jmp KERNEL_ENTRY_POINT

;--------------------------加载内核----------------------------
;输入：esi=内核加载地址
;-------------------------------------------------------------
kernel_init:
		
		push eax
		push ebx
		push ecx
		push edx
		
		xor edx,edx
		mov dx,[esi+42]		;偏移文件42处的地址是e_phentsize，表示program table的大小
		mov ebx,[esi+28]	;偏移28处是e_phoff，表示第一个program table在文件中的偏移量
		add ebx,esi
		xor ecx,ecx
		mov cx,[esi+44]		;偏移44处是e_phnum，表示有几个program header
		
	.each_segment:
		cmp dword [ebx+0x0],0	;比较program的pt_type是否为0,为0表示program header没有使用
		jz .PT_NULL
		
		push dword [ebx+16]	;program header偏移16字节的地方为p_filesz，表示此段在内存中的大小
		mov eax,[ebx+4]		;program header偏移4字节的地方为本段在文件中的偏移
		add eax,esi			;加上kernel的加载地址就是此段在内存中的虚拟地址
		push eax
		push dword [ebx+8]	;program header偏移8字节的地方是该段需要加载的目的地址
		call mem_cpy
		add esp,12			;维持栈平衡
	.PT_NULL:
		add ebx,edx
		loop .each_segment
		
		pop edx
		pop ecx
		pop ebx
		pop eax
		ret
		
;--------------------------------------------------------------
mem_cpy:
		
		push ecx
		push ebp
		push edi
		push esi
		
		cld 
		mov ebp,esp
		mov edi,[ebp+20]
		mov esi,[ebp+24]
		mov ecx,[ebp+28]
		rep movsb
		
		pop esi
		pop edi
		pop ebp
		pop ecx
		
		ret
		
;--------------------------------------------------------------
read_disk_m_32:
		
									;输入：eax=扇区号
									;	  ds:ebx=将读取的内容写到此处
									;	  ecx=需要读入的扇区数
		
		push eax
		push ecx
		push edx
		
		push eax
		
		;设置要读取的扇区数
		mov dx,0x1f2
		mov al,1
		out dx,al
		
		
		pop eax		;将扇区号从栈中弹出来
		;逻辑扇区号的低8位
		inc dx
		out dx,al
		;逻辑扇区号的中8位
		mov cl,8
		shr eax,cl
		inc dx
		out dx,al
		
		;逻辑扇区号的高8位
		shr eax,cl
		inc dx
		out dx,al
		
		;逻辑号的最高四位和device寄存器
		shr eax,cl
		or al,1110_0000B		;表示使用逻辑扇区，主盘
		inc dx
		out dx,al
		
		;发送读扇区命令
		mov al,0x20
		inc dx
		out dx,al
		
	.not_ready:
		nop 			;增加空指令，不要频繁的打扰硬盘
		in al,dx		;读status寄存器
		and al,0x88		;1000_1000，第一个1表示正忙，第二个1表示准备好
		cmp al,0x08
		jnz .not_ready
		
		;一次读取一个字，每个扇区512个字节，需要读取ax个扇区，所以读取次数为：512/2 * ax
		mov ecx,256
		
		;读取数据的端口号
		mov dx,0x1f0
	.read_data:
		in ax,dx
		mov [ebx],ax
		add ebx,2
		loop .read_data
		
		
		pop edx
		pop ecx
		pop eax
		
		ret

;--------------------------------------------------------------	
setup_page:

		push eax
		push ebx
		push ecx
		push esi
		push ds
		
		mov eax,DATA_SELECTOR
		mov ds,eax
		mov ebx,PAGE_DIR_TABLE_POS	;页目录的基地址
		mov ecx,1024
		xor esi,esi
	.clear_page:
		mov dword [ebx+esi*4],0		;清除页目录
		inc esi
		loop .clear_page
		
		
		mov eax,0x1000				
		add eax,ebx					;第一个页表的物理地址
		or eax,7					;所有程序可访问，可读可写，存在
		mov [ebx],eax				;设置第一个页表
		mov [ebx+0xc00],eax			;将内核映射到虚拟地址的3GB之上
		sub eax,0x1000
		mov [ebx+4092],ebx			;设置页目录的地址
		
		;映射1M一下内存
		mov ecx,256
		add ebx,0x1000
		xor eax,eax
		or eax,7
		xor esi,esi
	.set_1M_dir:
		mov [ebx+esi*4],eax
		add eax,0x1000
		inc esi
		loop .set_1M_dir
		
		sub ebx,0x1000			;回复页目录的基地址
		mov esi,769				;512+256=768，也就是说第768个页表是3GB内存的起始页表，我们已经更新，现在更新769-1022这些页表
		mov eax,ebx
		add eax,0x2000			;这是第二个页表的位置
		or eax,7
	.set_other_page:
		mov [ebx+esi*4],eax
		add eax,0x1000
		inc esi
		cmp esi,1023
		jl .set_other_page
		
		pop ds
		pop esi
		pop ecx
		pop ebx
		pop eax
		
		ret
		
;-------------------------------------------------------------
put_string:                                 ;显示0终止的字符串并移动光标 
                                            ;输入：DS:EBX=串地址
         push ecx
  .getc:
         mov cl,[ebx]
         or cl,cl
         jz .exit
         call put_char
         inc ebx
         jmp .getc

  .exit:
         pop ecx
         ret                               ;段间返回

;-------------------------------------------------------------------------------
put_char:                                   ;在当前光标处显示一个字符,并推进
                                            ;光标。仅用于段内调用 
                                            ;输入：CL=字符ASCII码 
         pushad

         ;以下取当前光标位置
         mov dx,0x3d4
         mov al,0x0e
         out dx,al
         inc dx                             ;0x3d5
         in al,dx                           ;高字
         mov ah,al

         dec dx                             ;0x3d4
         mov al,0x0f
         out dx,al
         inc dx                             ;0x3d5
         in al,dx                           ;低字
         mov bx,ax                          ;BX=代表光标位置的16位数

         cmp cl,0x0d                        ;回车符？
         jnz .put_0a
         mov ax,bx
         mov bl,80
         div bl
         mul bl
         mov bx,ax
         jmp .set_cursor

  .put_0a:
         cmp cl,0x0a                        ;换行符？
         jnz .put_other
         add bx,80
         jmp .roll_screen

  .put_other:                               ;正常显示字符
         push es
         mov eax,VIDEO_SELECTOR          ;0xb8000段的选择子
         mov es,eax
         shl bx,1
         mov [es:bx],cl
         pop es

         ;以下将光标位置推进一个字符
         shr bx,1
         inc bx

  .roll_screen:
         cmp bx,2000                        ;光标超出屏幕？滚屏
         jl .set_cursor

         push ds
         push es
         mov eax,VIDEO_SELECTOR
         mov ds,eax
         mov es,eax
         cld
         mov esi,0xa0                       ;小心！32位模式下movsb/w/d 
         mov edi,0x00                       ;使用的是esi/edi/ecx 
         mov ecx,1920
         rep movsd
         mov bx,3840                        ;清除屏幕最底一行
         mov ecx,80                         ;32位程序应该使用ECX
  .cls:
         mov word[es:bx],0x0720
         add bx,2
         loop .cls

         pop es
         pop ds

         mov bx,1920

  .set_cursor:
         mov dx,0x3d4
         mov al,0x0e
         out dx,al
         inc dx                             ;0x3d5
         mov al,bh
         out dx,al
         dec dx                             ;0x3d4
         mov al,0x0f
         out dx,al
         inc dx                             ;0x3d5
         mov al,bl
         out dx,al

         popad
         
         ret   

		
		
		
		
		
		
		
		
		
		
		
		
		
		
		
		


