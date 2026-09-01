.data

LStarHookCallback QWORD 0
LStarHookCallbackParam1 QWORD 0
LStarHookCallbackParam2 QWORD 0
LStarHookCallbackParam3 QWORD 0
OldLStarEntry QWORD 0

PUBLIC LStarHookCallback, OldLStarEntry, LStarHookCallbackParam1, LStarHookCallbackParam2, LStarHookCallbackParam3

.code

LStarHookEntry Proc
	swapgs
	;允许r0访问r3数据（禁用SMAP）
	stac
	;交换RSP
	mov qword ptr gs:[10h], rsp
	mov rsp, qword ptr gs:[1A8h]

	push 0
	push 0
	push 0
	push 0
	pushfq


	push rax
	push rbx
	push rcx
	push rdx
	push rdi
	push rsi
	push rbp
	push r8
	push r9
	push r10
	push r11
	push r12
	push r13
	push r14
	push r15
	sub rsp, 100h
	movaps xmmword ptr [rsp + 000h], xmm0
	movaps xmmword ptr [rsp + 010h], xmm1
	movaps xmmword ptr [rsp + 020h], xmm2
	movaps xmmword ptr [rsp + 030h], xmm3
	movaps xmmword ptr [rsp + 040h], xmm4
	movaps xmmword ptr [rsp + 050h], xmm5
	movaps xmmword ptr [rsp + 060h], xmm6
	movaps xmmword ptr [rsp + 070h], xmm7
	movaps xmmword ptr [rsp + 080h], xmm8
	movaps xmmword ptr [rsp + 090h], xmm9
	movaps xmmword ptr [rsp + 0A0h], xmm10
	movaps xmmword ptr [rsp + 0B0h], xmm11
	movaps xmmword ptr [rsp + 0C0h], xmm12
	movaps xmmword ptr [rsp + 0D0h], xmm13
	movaps xmmword ptr [rsp + 0E0h], xmm14
	movaps xmmword ptr [rsp + 0F0h], xmm15

	mov rcx, qword ptr gs:[10h]
	mov [rsp + 188h], rcx

	;填充参数并调用hook
	mov rcx, rsp
	mov rdx, [LStarHookCallbackParam1]
	mov r8, [LStarHookCallbackParam2]
	mov r9, [LStarHookCallbackParam3]
	
	call [LStarHookCallback]

	;还原参数并返回系统正常流程
	movaps xmm0, xmmword ptr [rsp + 000h]
	movaps xmm1, xmmword ptr [rsp + 010h]
	movaps xmm2, xmmword ptr [rsp + 020h]
	movaps xmm3, xmmword ptr [rsp + 030h]
	movaps xmm4, xmmword ptr [rsp + 040h]
	movaps xmm5, xmmword ptr [rsp + 050h]
	movaps xmm6, xmmword ptr [rsp + 060h]
	movaps xmm7, xmmword ptr [rsp + 070h]
	movaps xmm8, xmmword ptr [rsp + 080h]
	movaps xmm9, xmmword ptr [rsp + 090h]
	movaps xmm10, xmmword ptr [rsp + 0A0h]
	movaps xmm11, xmmword ptr [rsp + 0B0h]
	movaps xmm12, xmmword ptr [rsp + 0C0h]
	movaps xmm13, xmmword ptr [rsp + 0D0h]
	movaps xmm14, xmmword ptr [rsp + 0E0h]
	movaps xmm15, xmmword ptr [rsp + 0F0h]
	add rsp, 100h
	pop r15
	pop r14
	pop r13
	pop r12
	pop r11
	pop r10
	pop r9
	pop r8
	pop rbp
	pop rsi
	pop rdi
	pop rdx
	pop rcx
	pop rbx
	pop rax
	popfq
	add rsp, 20h
	;还原rsp
	mov rsp, qword ptr gs:[10h]
	;禁止r3访问r0数据（启用SMAP）
	clac
	;还原用户态gs
	swapgs

	;调用原有流程
	jmp [OldLStarEntry]

LStarHookEntry Endp

;允许r0访r3数据
_mystac Proc
stac
ret
_mystac Endp

;静止r0访问r3数据
_myclac Proc
clac
ret
_myclac Endp

End