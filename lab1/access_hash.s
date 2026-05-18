//__asm access_hash(char *str) 
	.global access_hash
	.p2align 2
	.type access_hash, %function


access_hash:
	.fnstart
//kwdikas gia upologismos tou megethous string

    MOVS r1, #0// r1 = 0 , o counter mou, dld h arxikh timh tou hash
    MOVS r3, r0 // r3 = &r0 = &str, r3 pernei timh tou r0 dioti metabaletai
str_size_loop:
    LDRB r2, [r3] // r2 = str[0], r0 = &str
    ADDS r3, #1 // r3 += 1 byte 
    CMP r2, #0 // an ftasoume sto telos tou string bgainoume apo to loop
    BEQ reset_pointer // an teleiwse to string tote bgainoume apo loop
    ADDS r1, #1 // r1 += 1
    B str_size_loop // alliws sunexizoume

reset_pointer:
	MOVS r3, r0 // ksana epanafora to r3 =&r0

hash_comp_loop:
    LDRB r2, [r3]   // r2 = str[r0]
    ADDS r3, #1 //  r3 += 1 byte
    CMP  r2, #0 // if r2 = str end
    BEQ  hash_comp_loop_end // end the loop


    CMP r2, #97 // 97 in ASCII is a
    BLT not_lower_case   
    
    CMP r2, #122 // 122 in ASCII is z
    BGT not_lower_case   

    PUSH {r4}
    MOVS r4, #0xDF
    AND  r2, r2, r4 // Bitwise AND tou r2
    POP {r4}
    ADDS r1, r1, r2 // prostetw to kwdika ASCII sto hash
    B hash_comp_loop

not_lower_case:
    CMP r2, #65 // 65 in ASCII is A 
    BLT not_upper_case 

    CMP r2, #90 // 90 in ASCII is Z
    BGT not_upper_case   

    LSL  r2, r2, #1 // left shift , dld r2 *= 2
    ADD r1, r2 // prostetw to kwdika ASCII sto hash
    B hash_comp_loop

not_upper_case: 
    CMP r2, #48 // 48 in ASCII is 0     
    BLT not_ASCII_number

    CMP r2, #57// 57 in ASCII is 9
    BGT not_ASCII_number

    PUSH {r4, r5}           // bazoume ton r4, r5 sto stack prosorina
    LDR  r5, =PrimeTable // r3 = &PrimeTable
    SUBS r2, r2, #48    // '0', '9' -- > 0 , 9
    LDRB r4, [r5, r2]   // r4 = PrimeTable[r2]
    ADDS  r1, r1, r4 // prosthetoume sto hash mia timh analoga to Look-up Table
    POP  {r4, r5} // pop = bgainei apo to stack o r4, r5
    B hash_comp_loop

not_ASCII_number:
    CMP r2, #0
    BEQ hash_comp_loop_end // an teleiwse to string tote bgainoume apo loop
    B hash_comp_loop // alliws sunexizoume

hash_comp_loop_end:
        LDR  r2, =FinalHashResult // r2 = &FinalHashResult
        STR  r1, [r2] // FinalHashResult = r1 = upologismeno hash value
        MOV r0, r1 // return hash value
        BX lr
	.fnend
	
.section .data, "aw"
    .p2align 2

PrimeTable:
    .byte 2, 3, 5, 7, 11, 13, 17, 19, 23, 29   
	.p2align 2
FinalHashResult:
    .long 0