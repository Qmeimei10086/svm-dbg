extern VmExitHandler : Proc
extern FillMachineFrame: Proc
extern CompareGenericRegisters: Proc

.code

BACKUP_REGISTERS Macro baseAddrReg
	movaps xmmword ptr [baseAddrReg + 000h], xmm0
	movaps xmmword ptr [baseAddrReg + 010h], xmm1
	movaps xmmword ptr [baseAddrReg + 020h], xmm2
	movaps xmmword ptr [baseAddrReg + 030h], xmm3
	movaps xmmword ptr [baseAddrReg + 040h], xmm4
	movaps xmmword ptr [baseAddrReg + 050h], xmm5
	movaps xmmword ptr [baseAddrReg + 060h], xmm6
	movaps xmmword ptr [baseAddrReg + 070h], xmm7
	movaps xmmword ptr [baseAddrReg + 080h], xmm8
	movaps xmmword ptr [baseAddrReg + 090h], xmm9
	movaps xmmword ptr [baseAddrReg + 0A0h], xmm10
	movaps xmmword ptr [baseAddrReg + 0B0h], xmm11
	movaps xmmword ptr [baseAddrReg + 0C0h], xmm12
	movaps xmmword ptr [baseAddrReg + 0D0h], xmm13
	movaps xmmword ptr [baseAddrReg + 0E0h], xmm14
	movaps xmmword ptr [baseAddrReg + 0F0h], xmm15
	mov [baseAddrReg + 100h], r15
	mov [baseAddrReg + 108h], r14
	mov [baseAddrReg + 110h], r13
	mov [baseAddrReg + 118h], r12
	mov [baseAddrReg + 120h], r11
	mov [baseAddrReg + 128h], r10
	mov [baseAddrReg + 130h], r9
	mov [baseAddrReg + 138h], r8
	mov [baseAddrReg + 140h], rbp
	mov [baseAddrReg + 148h], rsi
	mov [baseAddrReg + 150h], rdi
	mov [baseAddrReg + 158h], rdx
	mov [baseAddrReg + 160h], rcx
	mov [baseAddrReg + 168h], rbx
Endm

RESTORE_REGISTERS Macro baseAddrReg
	movaps xmm0, xmmword ptr [baseAddrReg + 000h]
	movaps xmm1, xmmword ptr [baseAddrReg + 010h]
	movaps xmm2, xmmword ptr [baseAddrReg + 020h]
	movaps xmm3, xmmword ptr [baseAddrReg + 030h]
	movaps xmm4, xmmword ptr [baseAddrReg + 040h]
	movaps xmm5, xmmword ptr [baseAddrReg + 050h]
	movaps xmm6, xmmword ptr [baseAddrReg + 060h]
	movaps xmm7, xmmword ptr [baseAddrReg + 070h]
	movaps xmm8, xmmword ptr [baseAddrReg + 080h]
	movaps xmm9, xmmword ptr [baseAddrReg + 090h]
	movaps xmm10, xmmword ptr [baseAddrReg + 0A0h]
	movaps xmm11, xmmword ptr [baseAddrReg + 0B0h] 
	movaps xmm12, xmmword ptr [baseAddrReg + 0C0h] 
	movaps xmm13, xmmword ptr [baseAddrReg + 0D0h]
	movaps xmm14, xmmword ptr [baseAddrReg + 0E0h]
	movaps xmm15, xmmword ptr [baseAddrReg + 0F0h]
	mov r15, [baseAddrReg + 100h]
	mov r14, [baseAddrReg + 108h]
	mov r13, [baseAddrReg + 110h]
	mov r12, [baseAddrReg + 118h]
	mov r11, [baseAddrReg + 120h]
	mov r10, [baseAddrReg + 128h]
	mov r9, [baseAddrReg + 130h] 
	mov r8, [baseAddrReg + 138h]
	mov rbp, [baseAddrReg + 140h]
	mov rsi, [baseAddrReg + 148h] 
	mov rdi, [baseAddrReg + 150h]
	mov rdx, [baseAddrReg + 158h]
	mov rcx, [baseAddrReg + 160h]
	mov rbx, [baseAddrReg + 168h]
Endm

ALLOC_STACK_AND_CALL Macro functionName, stackSize

sub rsp, stackSize
call functionName
add rsp, stackSize

Endm

_mysgdt Proc
	;执行存储寄存器数据只需要10个字节，这里方便一点
	sub rsp, 10h
	sgdt [rsp]
	;取limit
	mov ax, [rsp]
	mov word ptr [rdx], ax
	;取base
	mov rax, [rsp + 2]
	mov qword ptr [rcx], rax
	;还原栈
	add rsp, 10h
	ret
_mysgdt Endp

_mysidt Proc
;执行存储寄存器数据只需要10个字节，这里方便一点
	sub rsp, 10h
	sidt [rsp]
	;取limit
	mov ax, [rsp]
	mov word ptr [rdx], ax
	;取base
	mov rax, [rsp + 2]
	mov qword ptr [rcx], rax
	;还原栈
	add rsp, 10h
	ret
_mysidt Endp
	
_mysldt Proc
	sldt ax
	mov word ptr [rcx], ax
	ret
_mysldt Endp

_mystr Proc
	str ax
	mov word ptr [rcx], ax
	ret
_mystr Endp

_cs_selector Proc
	mov ax,cs
	ret
_cs_selector Endp

_ds_selector Proc
	mov ax,ds
	ret
_ds_selector Endp

_es_selector Proc
	mov ax,es
	ret
_es_selector Endp

_fs_selector Proc
	mov ax,fs
	ret
_fs_selector Endp

_gs_selector Proc
	mov ax,gs
	ret
_gs_selector Endp

_ss_selector Proc
	mov ax,ss
	ret
_ss_selector Endp

_save_or_load_regs Proc	
	BACKUP_REGISTERS rcx
	
	;取当前Rflags
	pushfq
	mov rax, [rsp]
	mov [rcx + 178h], rax
	popfq

	;把rip指向判断是否load寄存器的位置
	mov rax, offset if_load_regs
	mov [rcx + 180h], rax
	
	;取函数返回之后的第一条地址
	;如果是进入虚拟化之后执行到if_load_regs时，这个会作为load寄存器之后的执行地址
	mov rax, [rsp]
	mov [rcx + 190h], rax

	;取函数返回之后的rsp，加8抵消call压入的返回地址
	;如果是进入虚拟化之后执行到if_load_regs时，这个会作为最后还原的rsp
	mov rax, rsp
	add rax, 8h
	mov [rcx + 188h], rax

	;rax置0，跳过load寄存器
	;如果是进入虚拟化之后再次执行到if_load_regs
	;因为rax参数设置，会load寄存器

	mov rax, 0h

if_load_regs:
	test rax, rax
	jz return

	RESTORE_REGISTERS rax

	;rax 不还原
	;mov rax, [rcx + 170h]

	;还原rflags
	push qword ptr [rcx + 178h]
	popfq
	;还原rsp
	mov rsp, [rax + 188h]
	;还原rax
	mov rax, [rax + 170h]
	jmp qword ptr [rcx + 190h]

return:
	;还原rax
	mov rax, [rcx + 170h]
	ret
_save_or_load_regs Endp

_run_svm_vmrun Proc Frame
;备份原栈指针
mov rax, rsp

;切换栈
mov rsp, r9

;MACHINE_FRAME 结构，占用40字节
sub rsp, 28h
;多8字节好对齐到16字节
sub rsp, 8h

;把原来的栈指针压到内存中
;多一个push是为了rsp对齐到16
push rax
push rax

;备份参数
;pStack 参数
push r9
;pHostVmcbPhyAddr 参数
push r8
;pGuestVmcbPhyAddr 参数
push rdx
;pVirtCpuInfo 参数
push rcx

;从proc frame 到 .endprolog 的内容里面，rsp总共移动了0x60
;其中 MACHINE_FRAME 占用 40 (0x28) 字节
;然后是多余的 8(0x8) 字节 对齐字节
;然后是两份call本函数切换rsp之前的旧rsp值 16 (0x10) 字节（搞两份是为了16字节对齐）
;然后是四个参数的备份 32 (0x20) 字节
;接着 32 (0x20) 字节预留给本函数的退出分支做临时堆栈
;除去.pushframe 的 MACHINE_FRAME 占用的空间 其他的 全部计算为 .allocstack 的数值
;接下来是一些个人对.pushframe .allocstack 这类指令的理解
;windbg分析到 下面的call指令的返回地址之后 先加上 .allocstack 的 0x1f8 （跳过对这部分指令的处理），接着按照 .pushframe 读取 MACHINE_FRAME 结构
;.pushframe .allocstack 不需要和汇编命令对应
;windbg 只是倒过来挨个处理这些伪指令
;所以影响 windbg 对调用栈还原的之后这些指令内容和顺序
;和这些指令跟在哪些汇编指令之后没有任何关系
;所以我把这些伪指令集中写在这里并且 .allocstack 的数字 是多步使用栈的和

.pushframe
.allocstack 38h
.endprolog

enter_guest:

;载入guest状态
mov rax, [rsp + 8h]
vmload rax

;为了调用寄存器检查函数备份寄存器
push rcx
push rdx

;把当前寄存器数据载入pVirtCpuInfo->regsBackup.genericRegisters2
mov rax, [rsp + 10h]
add rax, 41A0h
BACKUP_REGISTERS rax

;CompareGenericRegisters 的两个参数
;rcx = &pVirtCpuInfo->regsBackup.genericRegisters2
;rdx = &pVirtCpuInfo->regsBackup.genericRegisters1
mov rcx, rax
mov rdx, rcx
sub rdx, 1A0h

;检查寄存器
ALLOC_STACK_AND_CALL CompareGenericRegisters, 30h

;调用完成，还原寄存器
pop rdx
pop rcx

;进入guest模式
mov rax, [rsp + 8h]
vmrun rax

;检查exitcode是否是 VMEXIT_INVALID(-1) VMEXIT_BUSY(-2) VMEXIT_IDLE_REQUIRED(-3)
;如果是，转到return标号，这个标号会负责切换会原栈指针并返回，返回之后直接蓝屏（见EnterVirtualization中调用RunVM之后的代码）
mov rax, [rsp]
mov rax, [rax + 70h]
cmp rax, -1
je return
cmp rax, -2
je return
cmp rax, -3
je return

;保存guest状态
mov rax, [rsp + 8h]
vmsave rax

;载入host状态
mov rax, [rsp + 10h]
vmload rax

;载入pVirtCpuInfo->regsBackup.genericRegisters1的地址
mov rax, [rsp]
add rax, 4000h
;备份guest寄存器
BACKUP_REGISTERS rax

;machineFrame 参数
mov rcx, rsp
add rcx, 38h
;guestRegisters 参数
mov rdx, [rsp]
add rdx, 4000h
;virtCpuInfo 参数
mov r8, [rsp]
;调用函数初始化MACHINE_FRAME
ALLOC_STACK_AND_CALL FillMachineFrame, 30h

;调用exit handler
;pVirtCpuInfo 参数
mov rcx, [rsp]
;pGuestRegisters 参数
mov rdx, rcx
add rdx, 4000h
;pGuestVmcbPhyAddr 参数
mov r8, [rsp + 8h]
;pHostVmcbPhyAddr 参数
mov r9, [rsp + 10h]

;进入Handler处理函数
ALLOC_STACK_AND_CALL VmExitHandler, 30h

;载入pVirtCpuInfo->regsBackup.genericRegisters1的地址
mov rax, [rsp]
add rax, 4000h

;恢复guest寄存器
RESTORE_REGISTERS rax

;判断是否已经退出虚拟化，需要跳转到guest的下一条指令
mov rax, [rax + 190h]
test rax, rax
;如果这时候转到退出vmm的分支rax的值仍是&pVMMVirtCpuInfo->regsBackup.genericRegisters1
jnz exit_virtualization

;保存host状态
mov rax, [rsp + 10h]
vmsave rax

;调回去，执行vmrun再次进入guest
jmp enter_guest

return:
;载入原来的栈指针并返回
mov rax, [rsp + 20h]
mov rsp, rax
ret

exit_virtualization:
;切换到客户机栈
mov rsp, [rax + 188h]
;push 客户机nRip
push [rax + 198h]
;push 客户机rax
push [rax + 170h]
;push 客户机rflags
push [rax + 178h]
;还原rflags
popfq
;还原rax
pop rax
;返回客户机nRip执行
ret

_run_svm_vmrun Endp

End