.global clearance_lvl
	.p2align 2
	.type clearance_lvl, %function


clearance_lvl:
	.fnstart
    MOVS r1, #0          // r1 counter asswn
    MOVS r2, #32         // r2 counter gia loop 32 bits

bit_count_loop:
    LSRS r0, r0, #1      // shift right, bit pou hanetai sto carry
    ADC  r1, r1, #0      // r1 = r1 + 0 + carry, an carry 1 r1 auksanetai alliws oxi
    SUBS r2, r2, #1      // bit counter -= 1
    BNE  bit_count_loop  // an r2 != 0  sunexise

mod_loop:
    CMP r1, #6 // sugrine r1 me 6
    BLT mod_loop_end // an mikrotero bges apo loop
    SUBS r1, r1, #6 // alliws afairese me to 6
    B mod_loop // sunexise mexri na broume to uoloipo

mod_loop_end:
    LDR r3, =Clearence_lvl // r3 = &Clearence_lvl
    STR r1, [r3] // learence_lvl = r1 = hash % 6
    MOV  r0, r1  // return modulo
    BX   lr
	.fnend
	
.section .data, "aw"
    .p2align 2
Clearence_lvl:
    .long 0