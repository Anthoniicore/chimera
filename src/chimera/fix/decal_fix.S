;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text

.globl _fix_decal_shaders_asm
_fix_decal_shaders_asm:
    push [esp + 76]
    call _set_up_pixel_shader_for_decals
    add esp, 4
    ret
