
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
