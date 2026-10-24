#!/bin/sh
# Make an LLVM (ld.lld) variant of a splat linker script.
# splat ends its script with `/DISCARD/ : { *(*); }`. GNU ld accepts that, but
# lld refuses to discard .shstrtab/.symtab/.strtab, so list the metadata
# sections the MIPS assembler emits instead of discarding everything.
# ld.lld also adds an empty two-entry .got for MIPS even when nothing uses
# it; dropping it keeps the output byte-identical.
# Usage: llvm_ldscript.sh <in.ld> <out.ld>
sed 's#^\(\s*\)\*(\*);#\1*(.MIPS.*) *(.reginfo) *(.pdr) *(.comment) *(.gnu.*) *(.mdebug*) *(.note*) *(.llvm_addrsig) *(.got)#' "$1" > "$2"
grep -q '\*(\*);' "$2" && { echo "catch-all discard still present in $2" >&2; exit 1; }
exit 0
