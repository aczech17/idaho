global set_cr3
global load_idtr
global invalidate_tlb_entry

set_cr3:
    mov cr3, rdi
    ret

load_idtr:
    lidt [rdi]
    ret

invalidate_tlb_entry:
    invlpg [rdi]
    ret