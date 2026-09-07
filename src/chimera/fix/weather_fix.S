;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text

.globl _new_weather_update_function
_new_weather_update_function:
    ;# Check if we can update
    push eax
    mov al, byte ptr [_can_update_weather]
    test al, al

    ;# If nonzero, we can update
    jnz update_weather

    ;# Otherwise, do nothing
    pushfd
    pushad
    call _meme_up_the_wind_globals
    popad
    popfd
    pop eax
    add esp, 20
    ret

    update_weather:

    ;# reset _can_update_weather value
    xor eax, eax
    mov byte ptr [_can_update_weather], al

    ;# Update
    pop eax
    jmp dword ptr [_original_call]