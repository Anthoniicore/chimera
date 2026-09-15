;# SPDX-License-Identifier: GPL-3.0-only

.intel_syntax noprefix

.text
.globl _magnetism_adhesion_hook

;# Codecave replacing the magnetism look-blend epilogue (~0x47428B).
;# Halo already computed the first-axis blend into st0 before jumping here.
;# We store both axis deltas then scale them by magnetism_adhesion_multiplier.
_magnetism_adhesion_hook:
    fstp dword ptr [esp + 0x34]
    fmul dword ptr [esp + 0x60]
    fld dword ptr [esp + 0x28]
    fmul dword ptr [esp + 0x38]
    faddp st(1), st
    fstp dword ptr [esp + 0x38]

    ;# Adhesion strength only (weapon magnetism angle/range unchanged)
    fld dword ptr [esp + 0x34]
    fmul dword ptr [_magnetism_adhesion_multiplier]
    fstp dword ptr [esp + 0x34]

    fld dword ptr [esp + 0x38]
    fmul dword ptr [_magnetism_adhesion_multiplier]
    fstp dword ptr [esp + 0x38]

    jmp dword ptr [_magnetism_adhesion_return]
