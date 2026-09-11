;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text
.globl _on_aim_assist
_on_aim_assist:
    mov eax, [_using_analog_movement]
    cmp byte ptr [eax], 1
    jnz not_actually_using_aim_assist
    jmp dword ptr [_yes_using_analog_movement_jmp]
    not_actually_using_aim_assist:
    jmp dword ptr [_not_using_analog_movement_jmp]

; write_jmp_call() calls this immediately before Halo's native final
; aim-assist scaling instruction at aim_assist + 0x357.
; Its pushfd/pushad wrapper adds 0x24 bytes to ESP, so the native
; correction values at [esp+0x34] and [esp+0x38] are [esp+0x58]
; and [esp+0x5c] while this callback is running.
.globl _on_aim_assist_strength
_on_aim_assist_strength:
    fld dword ptr [esp + 0x58]
    fmul dword ptr [_aim_assist_strength]
    fstp dword ptr [esp + 0x58]

    fld dword ptr [esp + 0x5C]
    fmul dword ptr [_aim_assist_strength]
    fstp dword ptr [esp + 0x5C]
    ret
