TI_GDT equ 0
RPL equ 0
SELECTOR_VIDEO equ (0x0003<<3) + TI_GDT + RPL 


section .data
put_int_buffer dq 0

[bits 32]
section .text

global put_str
put_str:
	push ebx
	push ecx
	push ebp
		
	mov ebp,esp
	mov ebx,[ebp+16]
	xor ecx,ecx	
    .goon:
    	mov cl,[ebx]
    	cmp cl,0
    	jz .str_over
	push ecx
	call put_char
	add esp,4
	inc ebx
	jmp .goon
	
    .str_over:
      pop ebp
    	pop ecx
    	pop ebx
    	ret
		
	
global put_char
put_char:
	pushad
	mov ax,SELECTOR_VIDEO
	mov gs,ax
	
	;获取当前光标位置
	mov dx,0x3d4	;索引寄存器
	mov al,0x0e	;用于提供光表位置的高8位
	out dx,al	
	mov dx,0x3d5	;通过读写寄存器获取光标位置
	in al,dx
	mov ah,al	
	
	mov dx,0x3d4
	mov al,0x0f
	out dx,al
	mov dx,0x3d5
	in al,dx
	
	mov bx,ax
	
	mov ebp,esp
	mov ecx,[ebp+36]	;8个通用的寄存器加上返回到源程序的地址
	cmp cl,0xd		;0xd表示CR回车符
	jz .is_carriage_return
	cmp cl,0xa		;0xa是LF换行符
	jz .is_line_feed
	
	cmp cl,0x8		;0x08是退格符
	jz .is_backspace	
	jmp .put_other

    .is_backspace:
    	dec bx
    	shl bx,1		;乘2
    	mov byte [gs:bx],0x20	;0x20是空格符号
    	inc bx			;加1填充字符属性
    	mov byte [gs:bx],0x07
    	shr bx,1		;除2获取光标，再加1获取下一次的光标
    	jmp .set_cursor
    	
    .put_other:
    	shl bx,1
    	mov [gs:bx],cl
    	inc bx
    	mov byte [gs:bx],0x07
    	shr bx,1
    	inc bx
    	cmp bx,2000
    	jl .set_cursor
    	
    .is_line_feed:
    .is_carriage_return:
    	xor dx,dx
    	mov ax,bx
    	mov si,80
    	div si
    	sub bx,dx	;当前行首位置
    	add bx,80
    	cmp bx,2000
    	jl .set_cursor
    	
    .roll_screen:
    	cld
    	mov esi,0xc00b80a0	;第二行是元操作数
    	mov edi,0xc00b8000	;第一行是目地操作数
    	mov ecx,960		;1920*2/4=960
    	rep movsd
    	
    	;清除最后一行
    	mov ebx,3840
    	mov ecx,80
    .cls:
    	mov word [gs:ebx],0x0720
    	add ebx,2
    	loop .cls
    	
    	mov bx,1920
    	
    .set_cursor:
    	mov dx,0x3d4
    	mov al,0x0e
    	out dx,al
    	mov dx,0x3d5
    	mov al,bh
    	out dx,al
    	
    	mov dx,0x3d4
    	mov al,0x0f
    	out dx,al
    	mov dx,0x3d5
    	mov al,bl
    	out dx,al
    	
    .put_char_done:
	popad
	ret 	
    	
global put_int
put_int:
	pushad
	mov ebp,esp
	mov eax,[ebp+36]
	mov edx,eax	;eax作为辅助，edx每次取4位
	mov ecx,8	;32数字有8个4为二进制
    	mov ebx,put_int_buffer
    	mov edi,7	;高字节存放数字的地位，“大端”
    	
    .16based_4bits:
    	and edx,0x0000000F
  	cmp edx,9
  	jg .isA_F
  	add edx,'0'
  	jmp .store
  	
    .isA_F:
    	sub edx,10	;得到距离字符'A'的偏移
    	add edx,'A'	;获取字符的ASCII马
    	
    .store:
    	mov [ebx+edi],dl
    	dec edi
    	shr eax,4	;将eretax中的数字每次右移动四位
    	mov edx,eax	;下一次的4bit数字
    	loop .16based_4bits
    	
    	inc edi	;退出循环时，edi为-1
    	
    	;下面去除数字中高位中的0，如0x0023=0x23
    .skip_prefix_0:
    	cmp edi,8	;如果edi为8，证明数字就是0
    	je .full0	;跳转
    	
    .go_on_skip:	
    	mov cl,[put_int_buffer+edi]	;获取当前位置的字符
    	inc edi			;增加edi，移动到下一字符
    	cmp cl,'0'
    	jz .skip_prefix_0		;为0则继续找
    	dec edi			;如果不为0，把edi加回来
    	jmp .put_each_num		;去输出字符
    	
    .full0:
    	mov cl,'0'
    	
    .put_each_num:
    	push ecx
    	call put_char
    	add esp,4
    	inc edi
    	mov cl,[put_int_buffer+edi]
    	cmp edi,8
    	jl .put_each_num
    
    	popad
    	ret
    	
global set_cursor
set_cursor:
   pushad
   mov bx, [esp+36]
;;;;;;; 1 先设置高8位 ;;;;;;;;
   mov dx, 0x03d4                         ;索引寄存器
   mov al, 0x0e                           ;用于提供光标位置的高8位
   out dx, al
   mov dx, 0x03d5                         ;通过读写数据端口0x3d5来获得或设置光标位置 
   mov al, bh
   out dx, al

;;;;;;; 2 再设置低8位 ;;;;;;;;;
   mov dx, 0x03d4
   mov al, 0x0f
   out dx, al
   mov dx, 0x03d5
   mov al, bl
   out dx, al
   popad
   ret

global cls_screen
cls_screen:
	pushad
	mov ax,SELECTOR_VIDEO
	mov gs,ax
	mov ebx,0
	mov ecx,80*25
.cls:
	mov word [gs:ebx],0x720
	add ebx,2
	loop .cls
	mov ebx,0
	;设置光标的位置
.set_cursor:
	mov dx,0x3d4
	mov al,0x0e
	out dx,al
	mov dx,0x3d5
	mov al,bh
	out dx,al
	mov dx, 0x03d4

    mov al, 0x0f
    out dx, al
    mov dx, 0x03d5
    mov al, bl
    out dx, al
    popad
    ret
    	
    	
    	
    	
