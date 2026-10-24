# This file is used by modern gas.
# Defines the expected assembly macros

# Evaluate this file only once in case it's included more than once
.ifndef _MACRO_INC_GUARD
.internal _MACRO_INC_GUARD
.set _MACRO_INC_GUARD, 1

# A function symbol.
.macro glabel label, visibility=global
    .\visibility \label
    .type \label, @function
    \label:
        # .ent omitted: debug-only, LLVM requires strict pairing
.endm

# The end of a function symbol.
.macro endlabel label
    .size \label, . - \label
    # .end omitted: debug-only, LLVM requires strict pairing
.endm

# An alternative entry to a function.
.macro alabel label, visibility=global
    .\visibility \label
    .type \label, @function
    \label:
        # .aent omitted: not supported by LLVM, emits no bytes
.endm

# A label referenced by an error handler table.
.macro ehlabel label, visibility=global
    .\visibility \label
    \label:
.endm


# A label referenced by a jumptable.
.macro jlabel label, visibility=global
    .\visibility \label
    \label:
.endm


# A data symbol.
.macro dlabel label, visibility=global
    .\visibility \label
    .type \label, @object
    \label:
.endm

# End of a data symbol.
.macro enddlabel label
    .size \label, . - \label
.endm


# Label to signal the symbol haven't been matched yet.
.macro nonmatching label, size=1
    .global \label\().NON_MATCHING
    .type \label\().NON_MATCHING, @object
    .size \label\().NON_MATCHING, \size
    \label\().NON_MATCHING:
.endm

.include "gte_macros.inc"

.endif

# LLVM has no `cop2 imm25` instruction (used by the GTE command macros in
# gte_macros.inc).  Encoding: COP2 opcode 010010, CO bit 25 set, 25-bit imm.
.macro cop2 imm
    .word 0x4A000000 | ((\imm) & 0x01FFFFFF)
.endm
