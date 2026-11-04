#!/system/bin/sh
# Conservative ELF header validator for the packaged ARM64 audio effect.
# This checks the loader-relevant ELF identity/header fields; it is NOT a
# cryptographic checksum and does not replace full readelf/loader validation.

ivanna_elf_field() {
    dd if="$1" bs=1 skip="$2" count="$3" 2>/dev/null |
        od -An -tx1 2>/dev/null | tr -d ' \n'
}

ivanna_elf_validate() {
    local file="$1"
    local size magic class data ident_version type machine version ehsize phentsize phnum

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
    phnum=$(ivanna_elf_field "$file" 56 2)

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
    elif [ "$phentsize" != "3800" ] || [ "$phnum" = "0000" ]; then
        IVANNA_ELF_REASON="invalid ELF64 program-header table"
    else
        IVANNA_ELF_REASON="ELF64 AArch64 ET_DYN header accepted"
        return 0
    fi
    return 1
}
