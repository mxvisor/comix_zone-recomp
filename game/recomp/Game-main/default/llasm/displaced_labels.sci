; jump table at 0x405100: the table base is 4 bytes before the first entry
; (0x405104), because the switch index is 1-based. The displacement shifts the
; fixup target of the `jmp` to the first table entry, so the operand refers
; to the data segment instead of pointing into the jmp instruction.
loc_405100,4
; jump table at 0x40B546: the same case (1-based index)
loc_40B546,4
