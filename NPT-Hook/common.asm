.code

SetRegsThenCpuid Proc
	;备份寄存器
	push rax
	push rbx
	push rcx
	push rdx
	push r8
	push r9

	;写入参数
	mov rax, [rcx]
	mov rbx, [rdx]
	mov rcx, [r8]
	mov rdx, [r9]

	cpuid

	;回写参数
	mov r9, [rsp + 18h]
	mov [r9], rax
	mov r9, [rsp + 10h]
	mov [r9], rbx
	mov r9, [rsp + 8h]
	mov [r9], rcx
	mov r9, [rsp + 0h]
	mov [r9], rdx

	;还原寄存器
	pop r9
	pop r8
	pop rdx
	pop rcx
	pop rbx
	pop rax
	
	ret
SetRegsThenCpuid Endp

End