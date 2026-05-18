//lucas_sequence
.global lucas_sequence
	.p2align 2
	.type lucas_sequence, %function


lucas_sequence:
	.fnstart
	CMP r0, #0 // an n = 0
    BNE check_n1 // alliws tsekare an n = 1
    MOV r0, #2          // return r0 = 2
    BX  lr

check_n1:
	CMP r0, #1 // an n = 1
	BNE recursive_step // anadromikh klhsh
	MOV r0, #1          // return r0 = 1
	BX  lr

recursive_step: // L(n) = L(n-1) + L(n-2)
	PUSH {r4, lr}       // gia anadromikh klhsh swzoume to r4 kai to lr
	MOV  r4, r0         // r4 = n

	// L(n-1)
	SUBS r0, r4, #1     // r0 = n - 1
	BL   lucas_sequence // r0 = L(n-1)
	
	PUSH {r0}           // apothhkeush tou L(n-1) sthn stoiba

	// L(n-2)
	SUBS r0, r4, #2     // r0 = n - 2
	BL   lucas_sequence // r0 = L(n-2)
	
	POP  {r1}           // r1 = L(n-1), logo FIFO stoibas
	ADD  r0, r0, r1     // r0 = L(n-2) + L(n-1)
	LDR r1, =FinalLucasCode
	STR r0, [r1]	// return lucas sequence result
	POP  {r4, lr}       // epanafora tou r4 kai tou lr
	BX lr
	.fnend
	
.section .data, "aw"
    .p2align 2
FinalLucasCode:         
    .long 0
