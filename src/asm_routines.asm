global set_cr3
global load_idtr
global disable_bios_cursor
global invalidate_tlb_entry
global pci_read_config
global pci_write_config

PCI_CONFIG_ADDRESS  equ 0xCF8
PCI_CONFIG_DATA     equ 0xCFC

set_cr3:
    mov cr3, rdi
    ret

load_idtr:
    lidt [rdi]
    ret

disable_bios_cursor:
    mov dx, 0x3D4   ; register select port
    mov al, 0x0A    ; cursor start register
    out dx, al

    inc dx          ; 0x3D5 - R/W on selected register

    in al, dx       ; read the register
    or al, 0x20     ; set bit 5 to disable cursor
    out dx, al

    ret

invalidate_tlb_entry:
    invlpg [rdi]
    ret

; RDI, RSI, RDX, RCX, R8, R9



;u32 pci_read_config(u8 bus, u8 device, u8 function, u8 offset);
pci_read_config:
    ; DIL - bus
    ; SIL - device
    ; DL - function
    ; CL - offset

    ; 0x80000000 | (bus << 16) | (device << 11) | (function << 8) | (offset & 0xFC)

    movzx edi, dil
    shl edi, 16

    movzx esi, sil
    shl esi, 11

    movzx edx, dl
    shl edx, 8

    movzx ecx, cl
    and ecx, 0xFC

    mov eax, 0x80000000 ; enable
    or eax, edi
    or eax, esi
    or eax, edx
    or eax, ecx

    mov dx, PCI_CONFIG_ADDRESS
    out dx, eax

    mov dx, PCI_CONFIG_DATA
    in eax, dx

    ret


; void pci_write_config(u8 bus, u8 device, u8 function, u8 offset, u16 value);
pci_write_config:
    ; DIL - bus
    ; SIL - device
    ; DL - function
    ; CL - offset
    ; R8W - value

    movzx edi, dil
    shl edi, 16

    movzx esi, sil
    shl esi, 11

    movzx edx, dl
    shl edx, 8

    movzx ecx, cl
    and ecx, 0xFC

    mov eax, 0x80000000 ; enable
    or eax, edi
    or eax, esi
    or eax, edx
    or eax, ecx

    mov dx, PCI_CONFIG_ADDRESS
    out dx, eax

    mov dx, PCI_CONFIG_DATA
    mov ax, r8w
    out dx, ax

    ret
