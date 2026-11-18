global disable_bios_cursor

disable_bios_cursor:
    mov dx, 0x3D4   ; register select port
    mov al, 0x0A    ; cursor start register
    out dx, al

    inc dx          ; 0x3D5 - R/W on selected register

    in al, dx       ; read the register
    or al, 0x20     ; set bit 5 to disable cursor
    out dx, al

    ret
