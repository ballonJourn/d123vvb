#!/bin/sh
# Compile the board sources exactly as Luban-Lite would (same include paths,
# defines and rtconfig.h), using a stock RISC-V GCC instead of the T-Head one.
# Needs: riscv64-unknown-elf-gcc, picolibc-riscv64-unknown-elf, scons 4.4.0.
# Run from luban-lite/. Leaves the tree as it found it.
set -eu
DEFCONFIG=d13x_demo88-nor_rt-thread_helloworld_defconfig
CFG=target/configs/$DEFCONFIG
OUT=$(mktemp -d)
cp "$CFG" "$OUT/defconfig.bak"
had_tc=0; [ -e toolchain/.ready ] && had_tc=1

cleanup() {
    cp "$OUT/defconfig.bak" "$CFG"
    if [ $had_tc = 0 ]; then rm -rf toolchain/bin toolchain/.ready; fi
    rm -f .config .config.old rtconfig.h
}
trap cleanup EXIT

cat >> "$CFG" <<'CFGEOF'
CONFIG_RT_USING_SAL=y
CONFIG_SAL_USING_LWIP=y
CONFIG_RT_USING_POSIX_FS=y
CONFIG_RT_USING_POSIX_POLL=y
CONFIG_RT_USING_POSIX_SELECT=y
CONFIG_RT_USING_POSIX_SOCKET=y
CONFIG_LPKG_USING_DATASERVER=y
CFGEOF

if [ $had_tc = 0 ]; then
    mkdir -p toolchain/bin
    for t in gcc g++ ar as ld objcopy objdump size nm strip; do
        ln -sf "$(command -v riscv64-unknown-elf-$t)" toolchain/bin/riscv64-unknown-elf-$t
    done
    printf 'Linux\nXuantie-900-gcc-elf-newlib-x86_64-V2.6.1\n' > toolchain/.ready
fi

scons --apply-def=$DEFCONFIG >/dev/null
grep -q '^#define LPKG_USING_DATASERVER' rtconfig.h || { echo "dataserver not enabled"; exit 1; }
scons -n --verbose > "$OUT/dry.txt" 2>&1
grep -E 'dataserver/rtthread/dataserver(_rtt)?\.c' "$OUT/dry.txt" > "$OUT/cmds.txt" || true
[ "$(wc -l < "$OUT/cmds.txt")" -eq 2 ] || { echo "compile commands not found"; tail "$OUT/dry.txt"; exit 1; }

i=0; rc=0
while read -r line; do
    i=$((i + 1))
    cmd=$(echo "$line" | sed -e 's/-march=rv32imafdc[a-z0-9_]*_xtheade/-march=rv32imafdc/' \
                             -e 's/-mno-dup-loop-header//' \
                             -e "s# -o [^ ]*\.o # -o $OUT/obj$i.o #")
    if eval "$cmd --specs=picolibc.specs -Wextra" > "$OUT/log$i.txt" 2>&1; then
        n=$(grep -cE 'dataserver[a-z_]*\.c:[0-9]+:[0-9]+: (warning|error)' "$OUT/log$i.txt" || true)
        echo "object $i: ok, $n warnings in dataserver sources"
        [ "$n" -eq 0 ] || { grep -E 'dataserver[a-z_]*\.c:' "$OUT/log$i.txt"; rc=1; }
    else
        echo "object $i: FAILED"; cat "$OUT/log$i.txt"; rc=1
    fi
done < "$OUT/cmds.txt"
riscv64-unknown-elf-size "$OUT"/obj*.o
exit $rc
