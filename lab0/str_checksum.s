.global str_checksum
	.p2align 2
	.type str_checksum, %function


str_checksum:
	.fnstart
	MOVS r1, #0xAA // r1 dekaeksadikh stathera
    MOVS r3 , #0 // arxikopoihsh accumulator

str_loop:
    LDRB r2, [r0] // r2 = str[r0] , r0 = &str
    CMP r2, #0 // an ftasw telos string 
    BEQ str_loop_end // phgaine sto telos

    EORS r2, r2, r1 // xor me salt
    EORS r3, r3, r2 // xor me accumulator

    ADDS r0, #1 // r0 += 1 byte
    B str_loop

str_loop_end:
    LDR r2, =CheckSumResult // r2 = &CheckSumResult
    STR  r3, [r2] // CheckSumResult = r3
    MOVS r0, r3 // retuen accumulator
    BX lr
	.fnend
	
.section .data, "aw"
    .p2align 2
CheckSumResult:
    .long 0