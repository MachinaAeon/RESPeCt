#!/bin/sh
# Rebuild the game's code files from the split assembly using the LLVM
# toolchain, and check each against the original from the user's own disc.
#
#   ./build.sh            build everything
#   ./build.sh kernel     build one target (kernel, enemprog, stagprog, test)
#
# LLVM is used instead of GNU binutils; tools/llvm_fixup.py and
# tools/llvm_ldscript.sh paper over the few PS1 instructions and GNU-only
# linker syntax LLVM doesn't support. Output must be byte-identical.
set -eu
cd "$(dirname "$0")"

AS="clang --target=mipsel-unknown-none-elf -march=mips1 -mabi=32 -mno-abicalls \
    -fno-pic -G0 -c -x assembler-with-cpp -Iinclude/llvm -Iinclude"

# target  splat-ld-script  disc-file  entry
TARGETS="kernel:kernel.bin.ld:KERNEL.BIN:0x80010B08
enemprog:enemprog.ld:ENEMPROG.BIN:
stagprog:stagprog.ld:STAGPROG.BIN:
test:test.ld:TEST.BIN:"

# Map an object path from a splat linker script back to its source and
# build it.  build/X.s.o or build/X.o <- X.s ; build/assets/Y.bin.o <- assets/Y.bin
build_obj() {
    obj=$1
    src=${obj#build/}; src=${src%.o}
    mkdir -p "$(dirname "$obj")"
    case $src in
        *.bin)
            # Wrap raw bytes with .incbin so the object gets the same o32 ABI
            # flags as the rest (llvm-objcopy -I binary marks it n64).
            wrap="build/incbin/${src%.bin}.s"
            mkdir -p "$(dirname "$wrap")"
            printf '.section .data, "aw"\n.incbin "%s"\n' "$src" > "$wrap"
            $AS "$wrap" -o "$obj" ;;
        *)
            case $src in *.s) ;; *) src=$src.s ;; esac
            fixed="build/llvm_asm/${src#asm/}"
            mkdir -p "$(dirname "$fixed")"
            python3 tools/llvm_fixup.py "$src" "$fixed" >/dev/null
            $AS "$fixed" -o "$obj" ;;
    esac
}

build_target() {
    name=$1 ld=$2 disc=$3 entry=$4
    objs=$(grep -oE 'build/[A-Za-z0-9_./-]+\.o' "$ld" | sort -u)
    for o in $objs; do build_obj "$o" || { echo "FAILED: $o" >&2; return 1; }; done

    syms="-T undefined_syms.txt"
    for f in "build/${name}_undefined_funcs_auto.txt" "build/${name}_undefined_syms_auto.txt"; do
        [ -f "$f" ] && syms="$syms -T $f"
    done
    [ -n "$entry" ] && e="-e $entry" || e="-e 0"

    tools/llvm_ldscript.sh "$ld" "build/$name.llvm.ld"
    link() {
        ld.lld -m elf32ltsmip --no-check-sections --error-limit=0 $e \
            -T "build/$name.llvm.ld" $syms "$@" \
            -Map "build/$name.map" -o "build/$name.elf" $objs
    }
    if ! link 2>"build/$name.link.log"; then
        # Addresses splat left without a label: place them by the address
        # in their name, then relink. Anything else is a real error.
        if grep -q "undefined symbol" "build/$name.link.log" &&
           tools/gen_undef_syms.py "build/${name}_undefined_by_addr.txt" \
               <"build/$name.link.log"; then
            link -T "build/${name}_undefined_by_addr.txt"
        else
            cat "build/$name.link.log" >&2
            return 1
        fi
    fi
    llvm-objcopy -O binary "build/$name.elf" "build/$disc"

    if cmp -s "build/$disc" "disc/$disc"; then
        echo "OK: build/$disc matches disc/$disc"
    else
        echo "MISMATCH: build/$disc differs from disc/$disc" >&2
        cmp "build/$disc" "disc/$disc" 2>&1 | head -1 >&2
        return 1
    fi
}

want=${1:-}
status=0
echo "$TARGETS" | while IFS=: read -r name ld disc entry; do
    [ -z "$want" ] || [ "$want" = "$name" ] || continue
    build_target "$name" "$ld" "$disc" "$entry" || exit 1
done || status=1
exit $status
