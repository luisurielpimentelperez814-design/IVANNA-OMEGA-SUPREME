#!/system/bin/sh
# Conservative ELF header validator for the packaged ARM64 audio effect.
# This checks the ELF identity and program-header bounds; it is NOT a
# cryptographic checksum and does not replace readelf/linker validation.

ivanna_elf_field() {
    dd if="$1" bs=1 skip="$2" count="$3" 2>/dev/null |
        od -An -tx1 2>/dev/null | tr -d ' \n'
}

ivanna_elf_validate() {
    local file="$1"
    local size magic class data ident_version type machine version ehsize phentsize
    local phnum_hex phoff phnum table_end

    IVANNA_ELF_REASON="unknown"
    if [ -z "$file" ] || [ ! -f "$file" ]; then
        IVANNA_ELF_REASON="file missing"
        return 1
    fi

    size=$(wc -c < "$file" 2>/dev/null | tr -d '[:space:]')
    case "$size" in ''|*[!0-9]*)
        IVANNA_ELF_REASON="file size unreadable"
        return 1
    esac
    if [ "$size" -lt 120 ]; then
        IVANNA_ELF_REASON="truncated ELF header/program-header table"
        return 1
    fi

    magic=$(ivanna_elf_field "$file" 0 4)
    class=$(ivanna_elf_field "$file" 4 1)
    data=$(ivanna_elf_field "$file" 5 1)
    ident_version=$(ivanna_elf_field "$file" 6 1)
    type=$(ivanna_elf_field "$file" 16 2)
    machine=$(ivanna_elf_field "$file" 18 2)
    version=$(ivanna_elf_field "$file" 20 4)
    ehsize=$(ivanna_elf_field "$file" 52 2)
    phentsize=$(ivanna_elf_field "$file" 54 2)
    phnum_hex=$(ivanna_elf_field "$file" 56 2)

    if [ "$magic" != "7f454c46" ]; then
        IVANNA_ELF_REASON="invalid ELF magic"
    elif [ "$class" != "02" ]; then
        IVANNA_ELF_REASON="ELF is not 64-bit"
    elif [ "$data" != "01" ]; then
        IVANNA_ELF_REASON="ELF is not little-endian"
    elif [ "$ident_version" != "01" ] || [ "$version" != "01000000" ]; then
        IVANNA_ELF_REASON="unsupported ELF version"
    elif [ "$type" != "0300" ]; then
        IVANNA_ELF_REASON="ELF is not a shared object (ET_DYN)"
    elif [ "$machine" != "b700" ]; then
        IVANNA_ELF_REASON="ELF machine is not AArch64"
    elif [ "$ehsize" != "4000" ]; then
        IVANNA_ELF_REASON="unexpected ELF64 header size"
    elif [ "$phentsize" != "3800" ] || [ "$phnum_hex" = "0000" ]; then
        IVANNA_ELF_REASON="invalid ELF64 program-header table"
    else
        # ELF64 e_phoff/e_phnum are little-endian. The target is AArch64 LE;
        # od's unsigned 64-/16-bit formats avoid lossy hex-string arithmetic.
        phoff=$(od -An -tu8 -j32 -N8 "$file" 2>/dev/null | tr -d '[:space:]')
        phnum=$(od -An -tu2 -j56 -N2 "$file" 2>/dev/null | tr -d '[:space:]')
        case "$phoff:$phnum" in
            *[!0-9:]*|:*)
                IVANNA_ELF_REASON="unreadable ELF64 program-header bounds"
                return 1
                ;;
        esac
        if [ "$phoff" -lt 64 ] || [ "$phoff" -gt "$size" ]; then
            IVANNA_ELF_REASON="program-header offset outside file"
            return 1
        fi
        table_end=$((phoff + 56 * phnum))
        if [ "$table_end" -gt "$size" ]; then
            IVANNA_ELF_REASON="program-header table exceeds file size"
            return 1
        fi
        IVANNA_ELF_REASON="ELF64 AArch64 ET_DYN header and program-header bounds accepted"
        return 0
    fi
    return 1
}
