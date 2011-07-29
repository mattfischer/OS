.section .text

.globl RunFirstAsm
.type RunFirstAsm,%function
RunFirstAsm:
	ldm r0, {r0-r15}
.size RunFirstAsm, . - RunFirstAsm

.globl SwitchToAsm
.type SwitchToAsm,%function
SwitchToAsm:
	stm r0, {r0-r15}
	adr r2, finish
	str r2, [r0, #60]
	ldm r1, {r0-r15}
	
finish:
	bx lr
.size SwitchToAsm, . - SwitchToAsm

.globl SetMMUBase
.type SetMMUBase,%function
SetMMUBase:
	mcr p15, 0, r0, c2, c0, 0

	mov r0, #0
	mcr p15, 0, r0, c8, c5, 0

	bx lr
.size SetMMUBase, . - SetMMUBase

.globl FlushTLB
.type FlushTLB,%function
FlushTLB:
	mov r0, #0
	mcr p15, 0, r0, c8, c5, 0
	bx lr
.size FlushTLB, . - FlushTLB

.globl EnterUser
.type EnterUser,%function
EnterUser:
	mrs r2, cpsr
	bic r2, #0xf
	msr spsr, r2

	sub r2, sp, #60
	mov r3, #0
	str r3, [r2, #0]
	str r3, [r2, #4]
	str r3, [r2, #8]
	str r3, [r2, #12]
	str r3, [r2, #16]
	str r3, [r2, #20]
	str r3, [r2, #24]
	str r3, [r2, #28]
	str r3, [r2, #32]
	str r3, [r2, #36]
	str r3, [r2, #40]
	str r3, [r2, #44]
	str r3, [r2, #48]
	str r1, [r2, #52]
	str r3, [r2, #56]

	mov lr, r0
	ldm r2, {r0-r14}^
	nop
	movs pc, lr

.size EnterUser, . - EnterUser