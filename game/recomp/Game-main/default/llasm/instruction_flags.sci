
; 0x403DA4 needs FL_CARRY | FL_PARITY | FL_ADJUST | FL_ZERO | FL_SIGN | FL_OVERFLOW across a region boundary
loc_403DC6,0x3F,0x00 ; jne    0x403da4
loc_403D9F,0x3F,0x3F ; mov    ecx,0x140
loc_403DA4,0x00,0x3F ; xchg   edi,esi


; 0x403D9F needs FL_CARRY | FL_PARITY | FL_ADJUST | FL_ZERO | FL_SIGN | FL_OVERFLOW across a region boundary
loc_403DDE,0x3F,0x00 ; jne    0x403d9f
loc_403D9A,0x3F,0x00 ; mov    ebx,0xe0


; 0x4043BF needs FL_CARRY | FL_PARITY | FL_ADJUST | FL_ZERO | FL_SIGN | FL_OVERFLOW across a region boundary
loc_4043DF,0x3F,0x00 ; jne    0x4043bf
loc_4043BD,0x3F,0x00 ; mov    ecx,ebx
loc_4043BF,0x00,0x3F ; xchg   edi,esi


; 0x4043E6 needs FL_CARRY | FL_PARITY | FL_ADJUST | FL_ZERO | FL_SIGN | FL_OVERFLOW across a region boundary
loc_40440E,0x3F,0x00 ; jne    0x4043e6
loc_4043E4,0x3F,0x00 ; mov    ecx,ebx
loc_4043E6,0x00,0x3F ; xchg   edi,esi

; 0x405274 needs FL_ZERO across a region boundary
loc_405274,0x00,0x08 ; movb 0xff, 0x628b3a

; 0x4057A0 needs FL_ZERO across a region boundary
loc_4057A0,0x00,0x08 ; push ax

; 0x406404 needs FL_ZERO across a region boundary
loc_406404,0x00,0x08 ; call 0x40638f

; 0x406D36 needs FL_ZERO across a region boundary
loc_406D36,0x00,0x08 ; push esi

; 0x407CA1 needs FL_ZERO across a region boundary
loc_407CA1,0x00,0x08 ; mov ax, 0x26

; 0x407D16 needs FL_ZERO across a region boundary
loc_407D16,0x00,0x08 ; mov ax, 0x3c

; 0x407D6D needs FL_ZERO across a region boundary
loc_407D6D,0x00,0x08 ; push esi

; 0x40F750 needs FL_ZERO across a region boundary
loc_40F750,0x00,0x08 ; mov ebp, esi

; the callback of the effect list (0x406630: call edi, then jne) returns its result in the flags
loc_407627,0x08,0x00 ; ret (after dec word [0x437f90])
loc_40760B,0x08,0x00 ; ret (after dec word [0x437f90]): the same callback of the fade in the other direction (0x4075f0)

; the allocator of the pool 0x437c70 (0x40638f) returns its result in ZF: the callers test it (jz after the call)
loc_4063E4,0x08,0x00 ; ret (after add esi, 0x2: the block is allocated, ZF is clear)
loc_4063EC,0x08,0x00 ; ret (after cmp ax, ax: no free block, ZF is set)
loc_406403,0x08,0x00 ; ret (after add esi, 0x2: the block is allocated, ZF is clear)
loc_406424,0x08,0x00 ; ret of the allocator that clears the block (0x406404)

; the callers of these routines test ZF after the call (jz/jnz), the flags of the last instruction must reach the ret
loc_405459,0x08,0x00 ; ret of 0x4053f2 (after or bx, 0x8000 / 0x1000: ZF is clear)
loc_4064F4,0x08,0x00 ; ret of 0x4064d0 (after test eax, eax)
loc_407D07,0x08,0x00 ; ret of 0x407ca5 / 0x407cab (after test esi, esi)
loc_407F36,0x08,0x00 ; ret of 0x407ea4 (after cmp dword [esi], 0x0)
loc_407FEA,0x08,0x00 ; ret of 0x407f37 / 0x407f43 (after cmp dword [edi], 0x0)
loc_40671C,0x08,0x00 ; ret of 0x40670a (after dec ebx)

; the handlers of the callback objects (list 0x629080): ZF set = the object is deleted (0x406632)
loc_405CA5,0x08,0x00 ; ret of the handler 0x405c6d (after cmp word [esi+0x14], 0x0)
loc_40EE0F,0x08,0x00 ; ret of the handler 0x40edef (after add dword [esi+0xe], 0x3: ZF is clear)

; the hit tests of the objects (0x407892 and 0x407955): the caller reads ZF after call
loc_4078FD,0x08,0x00 ; ret of 0x407892 (after dec byte [0x6290e1]: no hit)
loc_407927,0x08,0x00 ; ret of 0x407892 (after dec byte [0x6290e0])
loc_407954,0x08,0x00 ; ret of 0x407892 (after test eax, eax)
loc_407A18,0x08,0x00 ; ret of 0x407955 (after cmp byte [edi+0x4], 0xf6)
loc_407A29,0x08,0x00 ; ret of 0x407955 (after inc eax: ZF is clear, a hit)
loc_407A2C,0x08,0x00 ; ret of 0x407955 (after xor eax, eax: ZF is set, no hit)

; the update of an object (0x4087a0): 0x407e6e reads ZF after call
loc_40883F,0x08,0x00 ; ret (after test esi, esi)
loc_40884E,0x08,0x00 ; ret (after inc edi)
loc_408AC1,0x08,0x00 ; ret (after cmp eax, eax: ZF is set)
loc_408BCB,0x08,0x00 ; ret (after the flags of the earlier instructions)

; rets whose callers read ZF after the call: SRW's flag pass does not see through a call
loc_404CE2,0x08,0x00 ; ret of loc_404C96
loc_405347,0x08,0x00 ; ret of loc_4052F5
loc_4053C8,0x08,0x00 ; ret of loc_405348
loc_4053D4,0x08,0x00 ; ret of loc_4053C9 loc_4053CB
loc_4053F1,0x08,0x00 ; ret of loc_4053D5
loc_406310,0x08,0x00 ; ret of loc_4062F3 loc_406304
loc_406466,0x08,0x00 ; ret of loc_406425 loc_409A6B
loc_406481,0x08,0x00 ; ret of loc_406425 loc_409A6B
loc_406489,0x08,0x00 ; ret of loc_406425 loc_409A6B
loc_406511,0x08,0x00 ; ret of loc_4064D0
loc_406621,0x08,0x00 ; ret of loc_406608
loc_4066A3,0x08,0x00 ; ret of loc_4066A4
loc_4076C8,0x08,0x00 ; ret of loc_40525B loc_4076AD
loc_40775C,0x08,0x00 ; ret of loc_407713
loc_4077D6,0x08,0x00 ; ret of loc_407713
loc_407C7F,0x08,0x00 ; ret of loc_407C6B
loc_407D47,0x08,0x00 ; ret of loc_407D16
loc_4080A5,0x08,0x00 ; ret of loc_407EA4 loc_407F37 loc_407F43 loc_407FEB loc_408002
loc_4080A8,0x08,0x00 ; ret of loc_407EA4 loc_407F37 loc_407F43 loc_407FEB loc_408002

; the tasks of the scheduler list 0x629080 (loc_4065cd creates them): the dispatcher
; 0x406629 does "call edi" and 0x406632 reads ZF (0 = keep, 1 = unlink and free).
; The call is indirect, so SR's flag pass does not see the reader.
loc_404DBC,0x08,0x00 ; ret of the task 0x404db3 (after dec eax: ZF is clear)
loc_405E36,0x08,0x00 ; ret of the task 0x405e14 (after dec ax: ZF is clear)
loc_405E5C,0x08,0x00 ; ret of the task 0x405e54 (after test esi, esi)
loc_406925,0x08,0x00 ; ret of the task 0x40691b (after xor ax, ax: ZF is set)
loc_40E83B,0x08,0x00 ; ret of the task 0x40e7be (after dec eax: ZF is clear)
