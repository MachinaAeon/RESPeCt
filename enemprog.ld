SECTIONS
{
    HIDDEN(__romPos = 0);
    _gp = 0x80062D1C;
    ovl34_ROM_START = __romPos;
    ovl34_VRAM = ADDR(.ovl34);
    .ovl34 0x800F8000 : AT(ovl34_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl34_RODATA_START = .;
        build/asm/enemprog/data/ovl34_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl34.s.o(.rodata);
        ovl34_RODATA_END = .;
        ovl34_RODATA_SIZE = ABSOLUTE(ovl34_RODATA_END - ovl34_RODATA_START);
        ovl34_TEXT_START = .;
        build/asm/enemprog/ovl34.s.o(.text);
        ovl34_TEXT_END = .;
        ovl34_TEXT_SIZE = ABSOLUTE(ovl34_TEXT_END - ovl34_TEXT_START);
        ovl34_DATA_START = .;
        build/asm/enemprog/data/ovl34_header.rodata.s.o(.data);
        build/asm/enemprog/ovl34.s.o(.data);
        build/asm/enemprog/data/ovl34_data.data.s.o(.data);
        ovl34_DATA_END = .;
        ovl34_DATA_SIZE = ABSOLUTE(ovl34_DATA_END - ovl34_DATA_START);
        ovl34_BSS_START = .;
        build/asm/enemprog/data/ovl34_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl34.s.o(.bss);
        ovl34_BSS_END = .;
        ovl34_BSS_SIZE = ABSOLUTE(ovl34_BSS_END - ovl34_BSS_START);
    }
    __romPos += SIZEOF(.ovl34);
    __romPos = ALIGN(__romPos, 16);
    ovl34_ROM_END = __romPos;
    ovl34_VRAM_END = .;

    ovl35_ROM_START = __romPos;
    ovl35_VRAM = ADDR(.ovl35);
    .ovl35 0x800F8000 : AT(ovl35_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl35_RODATA_START = .;
        build/asm/enemprog/data/ovl35_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl35.s.o(.rodata);
        ovl35_RODATA_END = .;
        ovl35_RODATA_SIZE = ABSOLUTE(ovl35_RODATA_END - ovl35_RODATA_START);
        ovl35_TEXT_START = .;
        build/asm/enemprog/ovl35.s.o(.text);
        ovl35_TEXT_END = .;
        ovl35_TEXT_SIZE = ABSOLUTE(ovl35_TEXT_END - ovl35_TEXT_START);
        ovl35_DATA_START = .;
        build/asm/enemprog/data/ovl35_header.rodata.s.o(.data);
        build/asm/enemprog/ovl35.s.o(.data);
        build/asm/enemprog/data/ovl35_data.data.s.o(.data);
        ovl35_DATA_END = .;
        ovl35_DATA_SIZE = ABSOLUTE(ovl35_DATA_END - ovl35_DATA_START);
        ovl35_BSS_START = .;
        build/asm/enemprog/data/ovl35_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl35.s.o(.bss);
        ovl35_BSS_END = .;
        ovl35_BSS_SIZE = ABSOLUTE(ovl35_BSS_END - ovl35_BSS_START);
    }
    __romPos += SIZEOF(.ovl35);
    __romPos = ALIGN(__romPos, 16);
    ovl35_ROM_END = __romPos;
    ovl35_VRAM_END = .;

    ovl36_ROM_START = __romPos;
    ovl36_VRAM = ADDR(.ovl36);
    .ovl36 0x800F8000 : AT(ovl36_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl36_RODATA_START = .;
        build/asm/enemprog/data/ovl36_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl36.s.o(.rodata);
        ovl36_RODATA_END = .;
        ovl36_RODATA_SIZE = ABSOLUTE(ovl36_RODATA_END - ovl36_RODATA_START);
        ovl36_TEXT_START = .;
        build/asm/enemprog/ovl36.s.o(.text);
        ovl36_TEXT_END = .;
        ovl36_TEXT_SIZE = ABSOLUTE(ovl36_TEXT_END - ovl36_TEXT_START);
        ovl36_DATA_START = .;
        build/asm/enemprog/data/ovl36_header.rodata.s.o(.data);
        build/asm/enemprog/ovl36.s.o(.data);
        build/asm/enemprog/data/ovl36_data.data.s.o(.data);
        ovl36_DATA_END = .;
        ovl36_DATA_SIZE = ABSOLUTE(ovl36_DATA_END - ovl36_DATA_START);
        ovl36_BSS_START = .;
        build/asm/enemprog/data/ovl36_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl36.s.o(.bss);
        ovl36_BSS_END = .;
        ovl36_BSS_SIZE = ABSOLUTE(ovl36_BSS_END - ovl36_BSS_START);
    }
    __romPos += SIZEOF(.ovl36);
    __romPos = ALIGN(__romPos, 16);
    ovl36_ROM_END = __romPos;
    ovl36_VRAM_END = .;

    ovl37_ROM_START = __romPos;
    ovl37_VRAM = ADDR(.ovl37);
    .ovl37 0x800F8000 : AT(ovl37_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl37_RODATA_START = .;
        build/asm/enemprog/data/ovl37_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl37.s.o(.rodata);
        ovl37_RODATA_END = .;
        ovl37_RODATA_SIZE = ABSOLUTE(ovl37_RODATA_END - ovl37_RODATA_START);
        ovl37_TEXT_START = .;
        build/asm/enemprog/ovl37.s.o(.text);
        ovl37_TEXT_END = .;
        ovl37_TEXT_SIZE = ABSOLUTE(ovl37_TEXT_END - ovl37_TEXT_START);
        ovl37_DATA_START = .;
        build/asm/enemprog/data/ovl37_header.rodata.s.o(.data);
        build/asm/enemprog/ovl37.s.o(.data);
        build/asm/enemprog/data/ovl37_data.data.s.o(.data);
        ovl37_DATA_END = .;
        ovl37_DATA_SIZE = ABSOLUTE(ovl37_DATA_END - ovl37_DATA_START);
        ovl37_BSS_START = .;
        build/asm/enemprog/data/ovl37_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl37.s.o(.bss);
        ovl37_BSS_END = .;
        ovl37_BSS_SIZE = ABSOLUTE(ovl37_BSS_END - ovl37_BSS_START);
    }
    __romPos += SIZEOF(.ovl37);
    __romPos = ALIGN(__romPos, 16);
    ovl37_ROM_END = __romPos;
    ovl37_VRAM_END = .;

    ovl38_ROM_START = __romPos;
    ovl38_VRAM = ADDR(.ovl38);
    .ovl38 0x800F8000 : AT(ovl38_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl38_RODATA_START = .;
        build/asm/enemprog/data/ovl38_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl38.s.o(.rodata);
        ovl38_RODATA_END = .;
        ovl38_RODATA_SIZE = ABSOLUTE(ovl38_RODATA_END - ovl38_RODATA_START);
        ovl38_TEXT_START = .;
        build/asm/enemprog/ovl38.s.o(.text);
        ovl38_TEXT_END = .;
        ovl38_TEXT_SIZE = ABSOLUTE(ovl38_TEXT_END - ovl38_TEXT_START);
        ovl38_DATA_START = .;
        build/asm/enemprog/data/ovl38_header.rodata.s.o(.data);
        build/asm/enemprog/ovl38.s.o(.data);
        build/asm/enemprog/data/ovl38_data.data.s.o(.data);
        ovl38_DATA_END = .;
        ovl38_DATA_SIZE = ABSOLUTE(ovl38_DATA_END - ovl38_DATA_START);
        ovl38_BSS_START = .;
        build/asm/enemprog/data/ovl38_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl38.s.o(.bss);
        ovl38_BSS_END = .;
        ovl38_BSS_SIZE = ABSOLUTE(ovl38_BSS_END - ovl38_BSS_START);
    }
    __romPos += SIZEOF(.ovl38);
    __romPos = ALIGN(__romPos, 16);
    ovl38_ROM_END = __romPos;
    ovl38_VRAM_END = .;

    ovl39_ROM_START = __romPos;
    ovl39_VRAM = ADDR(.ovl39);
    .ovl39 0x800F8000 : AT(ovl39_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl39_RODATA_START = .;
        build/asm/enemprog/data/ovl39_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl39.s.o(.rodata);
        ovl39_RODATA_END = .;
        ovl39_RODATA_SIZE = ABSOLUTE(ovl39_RODATA_END - ovl39_RODATA_START);
        ovl39_TEXT_START = .;
        build/asm/enemprog/ovl39.s.o(.text);
        ovl39_TEXT_END = .;
        ovl39_TEXT_SIZE = ABSOLUTE(ovl39_TEXT_END - ovl39_TEXT_START);
        ovl39_DATA_START = .;
        build/asm/enemprog/data/ovl39_header.rodata.s.o(.data);
        build/asm/enemprog/ovl39.s.o(.data);
        build/asm/enemprog/data/ovl39_data.data.s.o(.data);
        ovl39_DATA_END = .;
        ovl39_DATA_SIZE = ABSOLUTE(ovl39_DATA_END - ovl39_DATA_START);
        ovl39_BSS_START = .;
        build/asm/enemprog/data/ovl39_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl39.s.o(.bss);
        ovl39_BSS_END = .;
        ovl39_BSS_SIZE = ABSOLUTE(ovl39_BSS_END - ovl39_BSS_START);
    }
    __romPos += SIZEOF(.ovl39);
    __romPos = ALIGN(__romPos, 16);
    ovl39_ROM_END = __romPos;
    ovl39_VRAM_END = .;

    ovl40_ROM_START = __romPos;
    ovl40_VRAM = ADDR(.ovl40);
    .ovl40 0x800F8000 : AT(ovl40_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl40_RODATA_START = .;
        build/asm/enemprog/data/ovl40_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl40.s.o(.rodata);
        ovl40_RODATA_END = .;
        ovl40_RODATA_SIZE = ABSOLUTE(ovl40_RODATA_END - ovl40_RODATA_START);
        ovl40_TEXT_START = .;
        build/asm/enemprog/ovl40.s.o(.text);
        ovl40_TEXT_END = .;
        ovl40_TEXT_SIZE = ABSOLUTE(ovl40_TEXT_END - ovl40_TEXT_START);
        ovl40_DATA_START = .;
        build/asm/enemprog/data/ovl40_header.rodata.s.o(.data);
        build/asm/enemprog/ovl40.s.o(.data);
        build/asm/enemprog/data/ovl40_data.data.s.o(.data);
        ovl40_DATA_END = .;
        ovl40_DATA_SIZE = ABSOLUTE(ovl40_DATA_END - ovl40_DATA_START);
        ovl40_BSS_START = .;
        build/asm/enemprog/data/ovl40_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl40.s.o(.bss);
        ovl40_BSS_END = .;
        ovl40_BSS_SIZE = ABSOLUTE(ovl40_BSS_END - ovl40_BSS_START);
    }
    __romPos += SIZEOF(.ovl40);
    __romPos = ALIGN(__romPos, 16);
    ovl40_ROM_END = __romPos;
    ovl40_VRAM_END = .;

    ovl41_ROM_START = __romPos;
    ovl41_VRAM = ADDR(.ovl41);
    .ovl41 0x800F8000 : AT(ovl41_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl41_RODATA_START = .;
        build/asm/enemprog/data/ovl41_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl41.s.o(.rodata);
        ovl41_RODATA_END = .;
        ovl41_RODATA_SIZE = ABSOLUTE(ovl41_RODATA_END - ovl41_RODATA_START);
        ovl41_TEXT_START = .;
        build/asm/enemprog/ovl41.s.o(.text);
        ovl41_TEXT_END = .;
        ovl41_TEXT_SIZE = ABSOLUTE(ovl41_TEXT_END - ovl41_TEXT_START);
        ovl41_DATA_START = .;
        build/asm/enemprog/data/ovl41_header.rodata.s.o(.data);
        build/asm/enemprog/ovl41.s.o(.data);
        build/asm/enemprog/data/ovl41_data.data.s.o(.data);
        ovl41_DATA_END = .;
        ovl41_DATA_SIZE = ABSOLUTE(ovl41_DATA_END - ovl41_DATA_START);
        ovl41_BSS_START = .;
        build/asm/enemprog/data/ovl41_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl41.s.o(.bss);
        ovl41_BSS_END = .;
        ovl41_BSS_SIZE = ABSOLUTE(ovl41_BSS_END - ovl41_BSS_START);
    }
    __romPos += SIZEOF(.ovl41);
    __romPos = ALIGN(__romPos, 16);
    ovl41_ROM_END = __romPos;
    ovl41_VRAM_END = .;

    ovl42_ROM_START = __romPos;
    ovl42_VRAM = ADDR(.ovl42);
    .ovl42 0x800F8000 : AT(ovl42_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl42_RODATA_START = .;
        build/asm/enemprog/data/ovl42_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl42.s.o(.rodata);
        ovl42_RODATA_END = .;
        ovl42_RODATA_SIZE = ABSOLUTE(ovl42_RODATA_END - ovl42_RODATA_START);
        ovl42_TEXT_START = .;
        build/asm/enemprog/ovl42.s.o(.text);
        ovl42_TEXT_END = .;
        ovl42_TEXT_SIZE = ABSOLUTE(ovl42_TEXT_END - ovl42_TEXT_START);
        ovl42_DATA_START = .;
        build/asm/enemprog/data/ovl42_header.rodata.s.o(.data);
        build/asm/enemprog/ovl42.s.o(.data);
        build/asm/enemprog/data/ovl42_data.data.s.o(.data);
        ovl42_DATA_END = .;
        ovl42_DATA_SIZE = ABSOLUTE(ovl42_DATA_END - ovl42_DATA_START);
        ovl42_BSS_START = .;
        build/asm/enemprog/data/ovl42_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl42.s.o(.bss);
        ovl42_BSS_END = .;
        ovl42_BSS_SIZE = ABSOLUTE(ovl42_BSS_END - ovl42_BSS_START);
    }
    __romPos += SIZEOF(.ovl42);
    __romPos = ALIGN(__romPos, 16);
    ovl42_ROM_END = __romPos;
    ovl42_VRAM_END = .;

    ovl43_ROM_START = __romPos;
    ovl43_VRAM = ADDR(.ovl43);
    .ovl43 0x800F8000 : AT(ovl43_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl43_RODATA_START = .;
        build/asm/enemprog/data/ovl43_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl43.s.o(.rodata);
        ovl43_RODATA_END = .;
        ovl43_RODATA_SIZE = ABSOLUTE(ovl43_RODATA_END - ovl43_RODATA_START);
        ovl43_TEXT_START = .;
        build/asm/enemprog/ovl43.s.o(.text);
        ovl43_TEXT_END = .;
        ovl43_TEXT_SIZE = ABSOLUTE(ovl43_TEXT_END - ovl43_TEXT_START);
        ovl43_DATA_START = .;
        build/asm/enemprog/data/ovl43_header.rodata.s.o(.data);
        build/asm/enemprog/ovl43.s.o(.data);
        build/asm/enemprog/data/ovl43_data.data.s.o(.data);
        ovl43_DATA_END = .;
        ovl43_DATA_SIZE = ABSOLUTE(ovl43_DATA_END - ovl43_DATA_START);
        ovl43_BSS_START = .;
        build/asm/enemprog/data/ovl43_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl43.s.o(.bss);
        ovl43_BSS_END = .;
        ovl43_BSS_SIZE = ABSOLUTE(ovl43_BSS_END - ovl43_BSS_START);
    }
    __romPos += SIZEOF(.ovl43);
    __romPos = ALIGN(__romPos, 16);
    ovl43_ROM_END = __romPos;
    ovl43_VRAM_END = .;

    ovl44_ROM_START = __romPos;
    ovl44_VRAM = ADDR(.ovl44);
    .ovl44 0x800F8000 : AT(ovl44_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl44_RODATA_START = .;
        build/asm/enemprog/data/ovl44_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl44.s.o(.rodata);
        ovl44_RODATA_END = .;
        ovl44_RODATA_SIZE = ABSOLUTE(ovl44_RODATA_END - ovl44_RODATA_START);
        ovl44_TEXT_START = .;
        build/asm/enemprog/ovl44.s.o(.text);
        ovl44_TEXT_END = .;
        ovl44_TEXT_SIZE = ABSOLUTE(ovl44_TEXT_END - ovl44_TEXT_START);
        ovl44_DATA_START = .;
        build/asm/enemprog/data/ovl44_header.rodata.s.o(.data);
        build/asm/enemprog/ovl44.s.o(.data);
        build/asm/enemprog/data/ovl44_data.data.s.o(.data);
        ovl44_DATA_END = .;
        ovl44_DATA_SIZE = ABSOLUTE(ovl44_DATA_END - ovl44_DATA_START);
        ovl44_BSS_START = .;
        build/asm/enemprog/data/ovl44_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl44.s.o(.bss);
        ovl44_BSS_END = .;
        ovl44_BSS_SIZE = ABSOLUTE(ovl44_BSS_END - ovl44_BSS_START);
    }
    __romPos += SIZEOF(.ovl44);
    __romPos = ALIGN(__romPos, 16);
    ovl44_ROM_END = __romPos;
    ovl44_VRAM_END = .;

    ovl45_ROM_START = __romPos;
    ovl45_VRAM = ADDR(.ovl45);
    .ovl45 0x800F8000 : AT(ovl45_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl45_RODATA_START = .;
        build/asm/enemprog/data/ovl45_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl45.s.o(.rodata);
        ovl45_RODATA_END = .;
        ovl45_RODATA_SIZE = ABSOLUTE(ovl45_RODATA_END - ovl45_RODATA_START);
        ovl45_TEXT_START = .;
        build/asm/enemprog/ovl45.s.o(.text);
        ovl45_TEXT_END = .;
        ovl45_TEXT_SIZE = ABSOLUTE(ovl45_TEXT_END - ovl45_TEXT_START);
        ovl45_DATA_START = .;
        build/asm/enemprog/data/ovl45_header.rodata.s.o(.data);
        build/asm/enemprog/ovl45.s.o(.data);
        build/asm/enemprog/data/ovl45_data.data.s.o(.data);
        ovl45_DATA_END = .;
        ovl45_DATA_SIZE = ABSOLUTE(ovl45_DATA_END - ovl45_DATA_START);
        ovl45_BSS_START = .;
        build/asm/enemprog/data/ovl45_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl45.s.o(.bss);
        ovl45_BSS_END = .;
        ovl45_BSS_SIZE = ABSOLUTE(ovl45_BSS_END - ovl45_BSS_START);
    }
    __romPos += SIZEOF(.ovl45);
    __romPos = ALIGN(__romPos, 16);
    ovl45_ROM_END = __romPos;
    ovl45_VRAM_END = .;

    ovl46_ROM_START = __romPos;
    ovl46_VRAM = ADDR(.ovl46);
    .ovl46 0x800F8000 : AT(ovl46_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl46_RODATA_START = .;
        build/asm/enemprog/data/ovl46_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl46.s.o(.rodata);
        ovl46_RODATA_END = .;
        ovl46_RODATA_SIZE = ABSOLUTE(ovl46_RODATA_END - ovl46_RODATA_START);
        ovl46_TEXT_START = .;
        build/asm/enemprog/ovl46.s.o(.text);
        ovl46_TEXT_END = .;
        ovl46_TEXT_SIZE = ABSOLUTE(ovl46_TEXT_END - ovl46_TEXT_START);
        ovl46_DATA_START = .;
        build/asm/enemprog/data/ovl46_header.rodata.s.o(.data);
        build/asm/enemprog/ovl46.s.o(.data);
        build/asm/enemprog/data/ovl46_data.data.s.o(.data);
        ovl46_DATA_END = .;
        ovl46_DATA_SIZE = ABSOLUTE(ovl46_DATA_END - ovl46_DATA_START);
        ovl46_BSS_START = .;
        build/asm/enemprog/data/ovl46_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl46.s.o(.bss);
        ovl46_BSS_END = .;
        ovl46_BSS_SIZE = ABSOLUTE(ovl46_BSS_END - ovl46_BSS_START);
    }
    __romPos += SIZEOF(.ovl46);
    __romPos = ALIGN(__romPos, 16);
    ovl46_ROM_END = __romPos;
    ovl46_VRAM_END = .;

    ovl47_ROM_START = __romPos;
    ovl47_VRAM = ADDR(.ovl47);
    .ovl47 0x800F8000 : AT(ovl47_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl47_RODATA_START = .;
        build/asm/enemprog/data/ovl47_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl47.s.o(.rodata);
        ovl47_RODATA_END = .;
        ovl47_RODATA_SIZE = ABSOLUTE(ovl47_RODATA_END - ovl47_RODATA_START);
        ovl47_TEXT_START = .;
        build/asm/enemprog/ovl47.s.o(.text);
        ovl47_TEXT_END = .;
        ovl47_TEXT_SIZE = ABSOLUTE(ovl47_TEXT_END - ovl47_TEXT_START);
        ovl47_DATA_START = .;
        build/asm/enemprog/data/ovl47_header.rodata.s.o(.data);
        build/asm/enemprog/ovl47.s.o(.data);
        build/asm/enemprog/data/ovl47_data.data.s.o(.data);
        ovl47_DATA_END = .;
        ovl47_DATA_SIZE = ABSOLUTE(ovl47_DATA_END - ovl47_DATA_START);
        ovl47_BSS_START = .;
        build/asm/enemprog/data/ovl47_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl47.s.o(.bss);
        ovl47_BSS_END = .;
        ovl47_BSS_SIZE = ABSOLUTE(ovl47_BSS_END - ovl47_BSS_START);
    }
    __romPos += SIZEOF(.ovl47);
    __romPos = ALIGN(__romPos, 16);
    ovl47_ROM_END = __romPos;
    ovl47_VRAM_END = .;

    ovl48_ROM_START = __romPos;
    ovl48_VRAM = ADDR(.ovl48);
    .ovl48 0x800F8000 : AT(ovl48_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl48_RODATA_START = .;
        build/asm/enemprog/data/ovl48_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl48.s.o(.rodata);
        ovl48_RODATA_END = .;
        ovl48_RODATA_SIZE = ABSOLUTE(ovl48_RODATA_END - ovl48_RODATA_START);
        ovl48_TEXT_START = .;
        build/asm/enemprog/ovl48.s.o(.text);
        ovl48_TEXT_END = .;
        ovl48_TEXT_SIZE = ABSOLUTE(ovl48_TEXT_END - ovl48_TEXT_START);
        ovl48_DATA_START = .;
        build/asm/enemprog/data/ovl48_header.rodata.s.o(.data);
        build/asm/enemprog/ovl48.s.o(.data);
        build/asm/enemprog/data/ovl48_data.data.s.o(.data);
        ovl48_DATA_END = .;
        ovl48_DATA_SIZE = ABSOLUTE(ovl48_DATA_END - ovl48_DATA_START);
        ovl48_BSS_START = .;
        build/asm/enemprog/data/ovl48_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl48.s.o(.bss);
        ovl48_BSS_END = .;
        ovl48_BSS_SIZE = ABSOLUTE(ovl48_BSS_END - ovl48_BSS_START);
    }
    __romPos += SIZEOF(.ovl48);
    __romPos = ALIGN(__romPos, 16);
    ovl48_ROM_END = __romPos;
    ovl48_VRAM_END = .;

    ovl50_ROM_START = __romPos;
    ovl50_VRAM = ADDR(.ovl50);
    .ovl50 0x80100000 : AT(ovl50_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl50_RODATA_START = .;
        build/asm/enemprog/data/ovl50_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl50.s.o(.rodata);
        ovl50_RODATA_END = .;
        ovl50_RODATA_SIZE = ABSOLUTE(ovl50_RODATA_END - ovl50_RODATA_START);
        ovl50_TEXT_START = .;
        build/asm/enemprog/ovl50.s.o(.text);
        ovl50_TEXT_END = .;
        ovl50_TEXT_SIZE = ABSOLUTE(ovl50_TEXT_END - ovl50_TEXT_START);
        ovl50_DATA_START = .;
        build/asm/enemprog/data/ovl50_header.rodata.s.o(.data);
        build/asm/enemprog/ovl50.s.o(.data);
        build/asm/enemprog/data/ovl50_data.data.s.o(.data);
        ovl50_DATA_END = .;
        ovl50_DATA_SIZE = ABSOLUTE(ovl50_DATA_END - ovl50_DATA_START);
        ovl50_BSS_START = .;
        build/asm/enemprog/data/ovl50_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl50.s.o(.bss);
        ovl50_BSS_END = .;
        ovl50_BSS_SIZE = ABSOLUTE(ovl50_BSS_END - ovl50_BSS_START);
    }
    __romPos += SIZEOF(.ovl50);
    __romPos = ALIGN(__romPos, 16);
    ovl50_ROM_END = __romPos;
    ovl50_VRAM_END = .;

    ovl51_ROM_START = __romPos;
    ovl51_VRAM = ADDR(.ovl51);
    .ovl51 0x80100000 : AT(ovl51_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl51_RODATA_START = .;
        build/asm/enemprog/data/ovl51_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl51.s.o(.rodata);
        ovl51_RODATA_END = .;
        ovl51_RODATA_SIZE = ABSOLUTE(ovl51_RODATA_END - ovl51_RODATA_START);
        ovl51_TEXT_START = .;
        build/asm/enemprog/ovl51.s.o(.text);
        ovl51_TEXT_END = .;
        ovl51_TEXT_SIZE = ABSOLUTE(ovl51_TEXT_END - ovl51_TEXT_START);
        ovl51_DATA_START = .;
        build/asm/enemprog/data/ovl51_header.rodata.s.o(.data);
        build/asm/enemprog/ovl51.s.o(.data);
        build/asm/enemprog/data/ovl51_data.data.s.o(.data);
        ovl51_DATA_END = .;
        ovl51_DATA_SIZE = ABSOLUTE(ovl51_DATA_END - ovl51_DATA_START);
        ovl51_BSS_START = .;
        build/asm/enemprog/data/ovl51_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl51.s.o(.bss);
        ovl51_BSS_END = .;
        ovl51_BSS_SIZE = ABSOLUTE(ovl51_BSS_END - ovl51_BSS_START);
    }
    __romPos += SIZEOF(.ovl51);
    __romPos = ALIGN(__romPos, 16);
    ovl51_ROM_END = __romPos;
    ovl51_VRAM_END = .;

    ovl52_ROM_START = __romPos;
    ovl52_VRAM = ADDR(.ovl52);
    .ovl52 0x80100000 : AT(ovl52_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl52_RODATA_START = .;
        build/asm/enemprog/data/ovl52_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl52.s.o(.rodata);
        ovl52_RODATA_END = .;
        ovl52_RODATA_SIZE = ABSOLUTE(ovl52_RODATA_END - ovl52_RODATA_START);
        ovl52_TEXT_START = .;
        build/asm/enemprog/ovl52.s.o(.text);
        ovl52_TEXT_END = .;
        ovl52_TEXT_SIZE = ABSOLUTE(ovl52_TEXT_END - ovl52_TEXT_START);
        ovl52_DATA_START = .;
        build/asm/enemprog/data/ovl52_header.rodata.s.o(.data);
        build/asm/enemprog/ovl52.s.o(.data);
        build/asm/enemprog/data/ovl52_data.data.s.o(.data);
        ovl52_DATA_END = .;
        ovl52_DATA_SIZE = ABSOLUTE(ovl52_DATA_END - ovl52_DATA_START);
        ovl52_BSS_START = .;
        build/asm/enemprog/data/ovl52_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl52.s.o(.bss);
        ovl52_BSS_END = .;
        ovl52_BSS_SIZE = ABSOLUTE(ovl52_BSS_END - ovl52_BSS_START);
    }
    __romPos += SIZEOF(.ovl52);
    __romPos = ALIGN(__romPos, 16);
    ovl52_ROM_END = __romPos;
    ovl52_VRAM_END = .;

    ovl53_ROM_START = __romPos;
    ovl53_VRAM = ADDR(.ovl53);
    .ovl53 0x80100000 : AT(ovl53_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl53_RODATA_START = .;
        build/asm/enemprog/data/ovl53_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl53.s.o(.rodata);
        ovl53_RODATA_END = .;
        ovl53_RODATA_SIZE = ABSOLUTE(ovl53_RODATA_END - ovl53_RODATA_START);
        ovl53_TEXT_START = .;
        build/asm/enemprog/ovl53.s.o(.text);
        ovl53_TEXT_END = .;
        ovl53_TEXT_SIZE = ABSOLUTE(ovl53_TEXT_END - ovl53_TEXT_START);
        ovl53_DATA_START = .;
        build/asm/enemprog/data/ovl53_header.rodata.s.o(.data);
        build/asm/enemprog/ovl53.s.o(.data);
        build/asm/enemprog/data/ovl53_data.data.s.o(.data);
        ovl53_DATA_END = .;
        ovl53_DATA_SIZE = ABSOLUTE(ovl53_DATA_END - ovl53_DATA_START);
        ovl53_BSS_START = .;
        build/asm/enemprog/data/ovl53_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl53.s.o(.bss);
        ovl53_BSS_END = .;
        ovl53_BSS_SIZE = ABSOLUTE(ovl53_BSS_END - ovl53_BSS_START);
    }
    __romPos += SIZEOF(.ovl53);
    __romPos = ALIGN(__romPos, 16);
    ovl53_ROM_END = __romPos;
    ovl53_VRAM_END = .;

    ovl54_ROM_START = __romPos;
    ovl54_VRAM = ADDR(.ovl54);
    .ovl54 0x80100000 : AT(ovl54_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl54_RODATA_START = .;
        build/asm/enemprog/data/ovl54_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl54.s.o(.rodata);
        ovl54_RODATA_END = .;
        ovl54_RODATA_SIZE = ABSOLUTE(ovl54_RODATA_END - ovl54_RODATA_START);
        ovl54_TEXT_START = .;
        build/asm/enemprog/ovl54.s.o(.text);
        ovl54_TEXT_END = .;
        ovl54_TEXT_SIZE = ABSOLUTE(ovl54_TEXT_END - ovl54_TEXT_START);
        ovl54_DATA_START = .;
        build/asm/enemprog/data/ovl54_header.rodata.s.o(.data);
        build/asm/enemprog/ovl54.s.o(.data);
        build/asm/enemprog/data/ovl54_data.data.s.o(.data);
        ovl54_DATA_END = .;
        ovl54_DATA_SIZE = ABSOLUTE(ovl54_DATA_END - ovl54_DATA_START);
        ovl54_BSS_START = .;
        build/asm/enemprog/data/ovl54_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl54.s.o(.bss);
        ovl54_BSS_END = .;
        ovl54_BSS_SIZE = ABSOLUTE(ovl54_BSS_END - ovl54_BSS_START);
    }
    __romPos += SIZEOF(.ovl54);
    __romPos = ALIGN(__romPos, 16);
    ovl54_ROM_END = __romPos;
    ovl54_VRAM_END = .;

    ovl55_ROM_START = __romPos;
    ovl55_VRAM = ADDR(.ovl55);
    .ovl55 0x80100000 : AT(ovl55_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl55_RODATA_START = .;
        build/asm/enemprog/data/ovl55_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl55.s.o(.rodata);
        ovl55_RODATA_END = .;
        ovl55_RODATA_SIZE = ABSOLUTE(ovl55_RODATA_END - ovl55_RODATA_START);
        ovl55_TEXT_START = .;
        build/asm/enemprog/ovl55.s.o(.text);
        ovl55_TEXT_END = .;
        ovl55_TEXT_SIZE = ABSOLUTE(ovl55_TEXT_END - ovl55_TEXT_START);
        ovl55_DATA_START = .;
        build/asm/enemprog/data/ovl55_header.rodata.s.o(.data);
        build/asm/enemprog/ovl55.s.o(.data);
        build/asm/enemprog/data/ovl55_data.data.s.o(.data);
        ovl55_DATA_END = .;
        ovl55_DATA_SIZE = ABSOLUTE(ovl55_DATA_END - ovl55_DATA_START);
        ovl55_BSS_START = .;
        build/asm/enemprog/data/ovl55_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl55.s.o(.bss);
        ovl55_BSS_END = .;
        ovl55_BSS_SIZE = ABSOLUTE(ovl55_BSS_END - ovl55_BSS_START);
    }
    __romPos += SIZEOF(.ovl55);
    __romPos = ALIGN(__romPos, 16);
    ovl55_ROM_END = __romPos;
    ovl55_VRAM_END = .;

    ovl56_ROM_START = __romPos;
    ovl56_VRAM = ADDR(.ovl56);
    .ovl56 0x80100000 : AT(ovl56_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl56_RODATA_START = .;
        build/asm/enemprog/data/ovl56_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl56.s.o(.rodata);
        ovl56_RODATA_END = .;
        ovl56_RODATA_SIZE = ABSOLUTE(ovl56_RODATA_END - ovl56_RODATA_START);
        ovl56_TEXT_START = .;
        build/asm/enemprog/ovl56.s.o(.text);
        ovl56_TEXT_END = .;
        ovl56_TEXT_SIZE = ABSOLUTE(ovl56_TEXT_END - ovl56_TEXT_START);
        ovl56_DATA_START = .;
        build/asm/enemprog/data/ovl56_header.rodata.s.o(.data);
        build/asm/enemprog/ovl56.s.o(.data);
        build/asm/enemprog/data/ovl56_data.data.s.o(.data);
        ovl56_DATA_END = .;
        ovl56_DATA_SIZE = ABSOLUTE(ovl56_DATA_END - ovl56_DATA_START);
        ovl56_BSS_START = .;
        build/asm/enemprog/data/ovl56_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl56.s.o(.bss);
        ovl56_BSS_END = .;
        ovl56_BSS_SIZE = ABSOLUTE(ovl56_BSS_END - ovl56_BSS_START);
    }
    __romPos += SIZEOF(.ovl56);
    __romPos = ALIGN(__romPos, 16);
    ovl56_ROM_END = __romPos;
    ovl56_VRAM_END = .;

    ovl57_ROM_START = __romPos;
    ovl57_VRAM = ADDR(.ovl57);
    .ovl57 0x80100000 : AT(ovl57_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl57_RODATA_START = .;
        build/asm/enemprog/data/ovl57_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl57.s.o(.rodata);
        ovl57_RODATA_END = .;
        ovl57_RODATA_SIZE = ABSOLUTE(ovl57_RODATA_END - ovl57_RODATA_START);
        ovl57_TEXT_START = .;
        build/asm/enemprog/ovl57.s.o(.text);
        ovl57_TEXT_END = .;
        ovl57_TEXT_SIZE = ABSOLUTE(ovl57_TEXT_END - ovl57_TEXT_START);
        ovl57_DATA_START = .;
        build/asm/enemprog/data/ovl57_header.rodata.s.o(.data);
        build/asm/enemprog/ovl57.s.o(.data);
        build/asm/enemprog/data/ovl57_data.data.s.o(.data);
        ovl57_DATA_END = .;
        ovl57_DATA_SIZE = ABSOLUTE(ovl57_DATA_END - ovl57_DATA_START);
        ovl57_BSS_START = .;
        build/asm/enemprog/data/ovl57_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl57.s.o(.bss);
        ovl57_BSS_END = .;
        ovl57_BSS_SIZE = ABSOLUTE(ovl57_BSS_END - ovl57_BSS_START);
    }
    __romPos += SIZEOF(.ovl57);
    __romPos = ALIGN(__romPos, 16);
    ovl57_ROM_END = __romPos;
    ovl57_VRAM_END = .;

    ovl58_ROM_START = __romPos;
    ovl58_VRAM = ADDR(.ovl58);
    .ovl58 0x80100000 : AT(ovl58_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl58_RODATA_START = .;
        build/asm/enemprog/data/ovl58_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl58.s.o(.rodata);
        ovl58_RODATA_END = .;
        ovl58_RODATA_SIZE = ABSOLUTE(ovl58_RODATA_END - ovl58_RODATA_START);
        ovl58_TEXT_START = .;
        build/asm/enemprog/ovl58.s.o(.text);
        ovl58_TEXT_END = .;
        ovl58_TEXT_SIZE = ABSOLUTE(ovl58_TEXT_END - ovl58_TEXT_START);
        ovl58_DATA_START = .;
        build/asm/enemprog/data/ovl58_header.rodata.s.o(.data);
        build/asm/enemprog/ovl58.s.o(.data);
        build/asm/enemprog/data/ovl58_data.data.s.o(.data);
        ovl58_DATA_END = .;
        ovl58_DATA_SIZE = ABSOLUTE(ovl58_DATA_END - ovl58_DATA_START);
        ovl58_BSS_START = .;
        build/asm/enemprog/data/ovl58_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl58.s.o(.bss);
        ovl58_BSS_END = .;
        ovl58_BSS_SIZE = ABSOLUTE(ovl58_BSS_END - ovl58_BSS_START);
    }
    __romPos += SIZEOF(.ovl58);
    __romPos = ALIGN(__romPos, 16);
    ovl58_ROM_END = __romPos;
    ovl58_VRAM_END = .;

    ovl59_ROM_START = __romPos;
    ovl59_VRAM = ADDR(.ovl59);
    .ovl59 0x80100000 : AT(ovl59_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl59_RODATA_START = .;
        build/asm/enemprog/data/ovl59_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl59.s.o(.rodata);
        ovl59_RODATA_END = .;
        ovl59_RODATA_SIZE = ABSOLUTE(ovl59_RODATA_END - ovl59_RODATA_START);
        ovl59_TEXT_START = .;
        build/asm/enemprog/ovl59.s.o(.text);
        ovl59_TEXT_END = .;
        ovl59_TEXT_SIZE = ABSOLUTE(ovl59_TEXT_END - ovl59_TEXT_START);
        ovl59_DATA_START = .;
        build/asm/enemprog/data/ovl59_header.rodata.s.o(.data);
        build/asm/enemprog/ovl59.s.o(.data);
        build/asm/enemprog/data/ovl59_data.data.s.o(.data);
        ovl59_DATA_END = .;
        ovl59_DATA_SIZE = ABSOLUTE(ovl59_DATA_END - ovl59_DATA_START);
        ovl59_BSS_START = .;
        build/asm/enemprog/data/ovl59_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl59.s.o(.bss);
        ovl59_BSS_END = .;
        ovl59_BSS_SIZE = ABSOLUTE(ovl59_BSS_END - ovl59_BSS_START);
    }
    __romPos += SIZEOF(.ovl59);
    __romPos = ALIGN(__romPos, 16);
    ovl59_ROM_END = __romPos;
    ovl59_VRAM_END = .;

    ovl60_ROM_START = __romPos;
    ovl60_VRAM = ADDR(.ovl60);
    .ovl60 0x80100000 : AT(ovl60_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl60_RODATA_START = .;
        build/asm/enemprog/data/ovl60_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl60.s.o(.rodata);
        ovl60_RODATA_END = .;
        ovl60_RODATA_SIZE = ABSOLUTE(ovl60_RODATA_END - ovl60_RODATA_START);
        ovl60_TEXT_START = .;
        build/asm/enemprog/ovl60.s.o(.text);
        ovl60_TEXT_END = .;
        ovl60_TEXT_SIZE = ABSOLUTE(ovl60_TEXT_END - ovl60_TEXT_START);
        ovl60_DATA_START = .;
        build/asm/enemprog/data/ovl60_header.rodata.s.o(.data);
        build/asm/enemprog/ovl60.s.o(.data);
        build/asm/enemprog/data/ovl60_data.data.s.o(.data);
        ovl60_DATA_END = .;
        ovl60_DATA_SIZE = ABSOLUTE(ovl60_DATA_END - ovl60_DATA_START);
        ovl60_BSS_START = .;
        build/asm/enemprog/data/ovl60_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl60.s.o(.bss);
        ovl60_BSS_END = .;
        ovl60_BSS_SIZE = ABSOLUTE(ovl60_BSS_END - ovl60_BSS_START);
    }
    __romPos += SIZEOF(.ovl60);
    __romPos = ALIGN(__romPos, 16);
    ovl60_ROM_END = __romPos;
    ovl60_VRAM_END = .;

    ovl61_ROM_START = __romPos;
    ovl61_VRAM = ADDR(.ovl61);
    .ovl61 0x80100000 : AT(ovl61_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl61_RODATA_START = .;
        build/asm/enemprog/data/ovl61_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl61.s.o(.rodata);
        ovl61_RODATA_END = .;
        ovl61_RODATA_SIZE = ABSOLUTE(ovl61_RODATA_END - ovl61_RODATA_START);
        ovl61_TEXT_START = .;
        build/asm/enemprog/ovl61.s.o(.text);
        ovl61_TEXT_END = .;
        ovl61_TEXT_SIZE = ABSOLUTE(ovl61_TEXT_END - ovl61_TEXT_START);
        ovl61_DATA_START = .;
        build/asm/enemprog/data/ovl61_header.rodata.s.o(.data);
        build/asm/enemprog/ovl61.s.o(.data);
        build/asm/enemprog/data/ovl61_data.data.s.o(.data);
        ovl61_DATA_END = .;
        ovl61_DATA_SIZE = ABSOLUTE(ovl61_DATA_END - ovl61_DATA_START);
        ovl61_BSS_START = .;
        build/asm/enemprog/data/ovl61_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl61.s.o(.bss);
        ovl61_BSS_END = .;
        ovl61_BSS_SIZE = ABSOLUTE(ovl61_BSS_END - ovl61_BSS_START);
    }
    __romPos += SIZEOF(.ovl61);
    __romPos = ALIGN(__romPos, 16);
    ovl61_ROM_END = __romPos;
    ovl61_VRAM_END = .;

    ovl62_ROM_START = __romPos;
    ovl62_VRAM = ADDR(.ovl62);
    .ovl62 0x80100000 : AT(ovl62_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl62_RODATA_START = .;
        build/asm/enemprog/data/ovl62_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl62.s.o(.rodata);
        ovl62_RODATA_END = .;
        ovl62_RODATA_SIZE = ABSOLUTE(ovl62_RODATA_END - ovl62_RODATA_START);
        ovl62_TEXT_START = .;
        build/asm/enemprog/ovl62.s.o(.text);
        ovl62_TEXT_END = .;
        ovl62_TEXT_SIZE = ABSOLUTE(ovl62_TEXT_END - ovl62_TEXT_START);
        ovl62_DATA_START = .;
        build/asm/enemprog/data/ovl62_header.rodata.s.o(.data);
        build/asm/enemprog/ovl62.s.o(.data);
        build/asm/enemprog/data/ovl62_data.data.s.o(.data);
        ovl62_DATA_END = .;
        ovl62_DATA_SIZE = ABSOLUTE(ovl62_DATA_END - ovl62_DATA_START);
        ovl62_BSS_START = .;
        build/asm/enemprog/data/ovl62_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl62.s.o(.bss);
        ovl62_BSS_END = .;
        ovl62_BSS_SIZE = ABSOLUTE(ovl62_BSS_END - ovl62_BSS_START);
    }
    __romPos += SIZEOF(.ovl62);
    __romPos = ALIGN(__romPos, 16);
    ovl62_ROM_END = __romPos;
    ovl62_VRAM_END = .;

    ovl63_ROM_START = __romPos;
    ovl63_VRAM = ADDR(.ovl63);
    .ovl63 0x80100000 : AT(ovl63_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl63_RODATA_START = .;
        build/asm/enemprog/data/ovl63_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl63.s.o(.rodata);
        ovl63_RODATA_END = .;
        ovl63_RODATA_SIZE = ABSOLUTE(ovl63_RODATA_END - ovl63_RODATA_START);
        ovl63_TEXT_START = .;
        build/asm/enemprog/ovl63.s.o(.text);
        ovl63_TEXT_END = .;
        ovl63_TEXT_SIZE = ABSOLUTE(ovl63_TEXT_END - ovl63_TEXT_START);
        ovl63_DATA_START = .;
        build/asm/enemprog/data/ovl63_header.rodata.s.o(.data);
        build/asm/enemprog/ovl63.s.o(.data);
        build/asm/enemprog/data/ovl63_data.data.s.o(.data);
        ovl63_DATA_END = .;
        ovl63_DATA_SIZE = ABSOLUTE(ovl63_DATA_END - ovl63_DATA_START);
        ovl63_BSS_START = .;
        build/asm/enemprog/data/ovl63_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl63.s.o(.bss);
        ovl63_BSS_END = .;
        ovl63_BSS_SIZE = ABSOLUTE(ovl63_BSS_END - ovl63_BSS_START);
    }
    __romPos += SIZEOF(.ovl63);
    __romPos = ALIGN(__romPos, 16);
    ovl63_ROM_END = __romPos;
    ovl63_VRAM_END = .;

    ovl64_ROM_START = __romPos;
    ovl64_VRAM = ADDR(.ovl64);
    .ovl64 0x80100000 : AT(ovl64_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl64_RODATA_START = .;
        build/asm/enemprog/data/ovl64_header.rodata.s.o(.rodata);
        build/asm/enemprog/ovl64.s.o(.rodata);
        ovl64_RODATA_END = .;
        ovl64_RODATA_SIZE = ABSOLUTE(ovl64_RODATA_END - ovl64_RODATA_START);
        ovl64_TEXT_START = .;
        build/asm/enemprog/ovl64.s.o(.text);
        ovl64_TEXT_END = .;
        ovl64_TEXT_SIZE = ABSOLUTE(ovl64_TEXT_END - ovl64_TEXT_START);
        ovl64_DATA_START = .;
        build/asm/enemprog/data/ovl64_header.rodata.s.o(.data);
        build/asm/enemprog/ovl64.s.o(.data);
        build/asm/enemprog/data/ovl64_data.data.s.o(.data);
        ovl64_DATA_END = .;
        ovl64_DATA_SIZE = ABSOLUTE(ovl64_DATA_END - ovl64_DATA_START);
        ovl64_BSS_START = .;
        build/asm/enemprog/data/ovl64_header.rodata.s.o(.bss);
        build/asm/enemprog/ovl64.s.o(.bss);
        ovl64_BSS_END = .;
        ovl64_BSS_SIZE = ABSOLUTE(ovl64_BSS_END - ovl64_BSS_START);
    }
    __romPos += SIZEOF(.ovl64);
    __romPos = ALIGN(__romPos, 16);
    ovl64_ROM_END = __romPos;
    ovl64_VRAM_END = .;

    /DISCARD/ :
    {
        *(*);
    }
}
