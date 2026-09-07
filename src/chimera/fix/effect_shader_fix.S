;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text

.global _effect_shader_reindex_pixel_shader_asm
_effect_shader_reindex_pixel_shader_asm:
    pushfd
    pushad
    push ebx
    call _effect_shader_reindex_pixel_shader
    add esp, 4
    popad
    popfd
    mov eax, dword ptr [_effect_shader_permutation_index]
    jmp dword ptr [_effect_shader_get_ps_original]

.global _effect_shader_set_up_zsprite_custom_asm
_effect_shader_set_up_zsprite_custom_asm:
    push ebx
    call _set_up_zsprites
    add esp, 4
    ret

.global _shader_set_up_zprite_retail_asm
_shader_set_up_zprite_retail_asm:
    pushfd
    pushad
    mov byte ptr [_zsprite_drawn], 0
    push ebx
    call _set_up_zsprites
    add esp, 4
    popad
    popfd

    cmp byte ptr [_zsprite_drawn], 1
    je exit_shader
    jmp dword ptr [_shader_effect_d3dx_begin]

    exit_shader:
    add esp, 12
    jmp dword ptr [_shader_effect_draw_end]
