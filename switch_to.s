.section .text
.globl switchTo
.type switchTo,%function
switchTo:
	cmp r0, #0
	beq switch
	mov r2, r0
	stm r0, {r0-r15}
	ldr r2, finish
	str r2, [r0, #60]

switch:
	ldm r1, {r0-r15}
	
finish:
	bx lr
.size switchTo, .-switchTo

