[bits 32]
section .text
global switch_to
switch_to:
	push esi
	push edi
	push ebx
	push ebp
	mov eax,[esp+20] ;获取cur线程pcb基地址
	mov [eax],esp

	;----------------next线程开始执行------------
	mov eax,[esp+24]
	mov esp,[eax]	;获取next线程的基地址
	pop ebp
	pop ebx
	pop edi
	pop esi
	ret
	
