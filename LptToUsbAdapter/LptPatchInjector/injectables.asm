.686
.MODEL flat

PUBLIC _out_byte_fn
PUBLIC _out_byte_fn_size
PUBLIC _in_byte_fn
PUBLIC _in_byte_fn_size

.code
_out_byte_fn:
out_byte_external_fn dd 0
    PUSHAD
    CALL $+5
    POP ECX
    SUB ECX, 10
    MOV ECX, [ECX]
    
    AND EAX, 0FFh
    AND EDX, 0FFFFh
    PUSH EAX
    PUSH EDX
    CALL ECX
    ADD ESP, 8

    POPAD
out_byte_fn_end:

_in_byte_fn:
in_byte_external_fn dd 0
    SUB ESP, 4
    PUSHAD
    CALL $+5
    POP ECX
    SUB ECX, 13
    MOV ECX, [ECX]

    AND EDX, 0FFFFh

    PUSH EDX
    CALL ECX
    ADD ESP, 4

    mov [ESP+32], al
    POPAD
    MOV AL, [ESP+0]
    ADD ESP, 4
in_byte_fn_end:

.data
_out_byte_fn_size dd out_byte_fn_end - _out_byte_fn
_in_byte_fn_size dd in_byte_fn_end - _in_byte_fn

END