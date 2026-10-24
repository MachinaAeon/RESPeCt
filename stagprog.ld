SECTIONS
{
    HIDDEN(__romPos = 0);
    _gp = 0x80062D1C;
    ovl15_ROM_START = __romPos;
    ovl15_VRAM = ADDR(.ovl15);
    .ovl15 0x800F2C50 : AT(ovl15_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl15_RODATA_START = .;
        build/asm/stagprog/data/ovl15_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl15.s.o(.rodata);
        ovl15_RODATA_END = .;
        ovl15_RODATA_SIZE = ABSOLUTE(ovl15_RODATA_END - ovl15_RODATA_START);
        ovl15_TEXT_START = .;
        build/asm/stagprog/ovl15.s.o(.text);
        ovl15_TEXT_END = .;
        ovl15_TEXT_SIZE = ABSOLUTE(ovl15_TEXT_END - ovl15_TEXT_START);
        ovl15_DATA_START = .;
        build/asm/stagprog/data/ovl15_header.rodata.s.o(.data);
        build/asm/stagprog/ovl15.s.o(.data);
        build/asm/stagprog/data/ovl15_data.data.s.o(.data);
        ovl15_DATA_END = .;
        ovl15_DATA_SIZE = ABSOLUTE(ovl15_DATA_END - ovl15_DATA_START);
        ovl15_BSS_START = .;
        build/asm/stagprog/data/ovl15_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl15.s.o(.bss);
        ovl15_BSS_END = .;
        ovl15_BSS_SIZE = ABSOLUTE(ovl15_BSS_END - ovl15_BSS_START);
    }
    __romPos += SIZEOF(.ovl15);
    __romPos = ALIGN(__romPos, 16);
    ovl15_ROM_END = __romPos;
    ovl15_VRAM_END = .;

    ovl16_ROM_START = __romPos;
    ovl16_VRAM = ADDR(.ovl16);
    .ovl16 0x800F2C50 : AT(ovl16_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl16_RODATA_START = .;
        build/asm/stagprog/data/ovl16_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl16.s.o(.rodata);
        ovl16_RODATA_END = .;
        ovl16_RODATA_SIZE = ABSOLUTE(ovl16_RODATA_END - ovl16_RODATA_START);
        ovl16_TEXT_START = .;
        build/asm/stagprog/ovl16.s.o(.text);
        ovl16_TEXT_END = .;
        ovl16_TEXT_SIZE = ABSOLUTE(ovl16_TEXT_END - ovl16_TEXT_START);
        ovl16_DATA_START = .;
        build/asm/stagprog/data/ovl16_header.rodata.s.o(.data);
        build/asm/stagprog/ovl16.s.o(.data);
        build/asm/stagprog/data/ovl16_data.data.s.o(.data);
        ovl16_DATA_END = .;
        ovl16_DATA_SIZE = ABSOLUTE(ovl16_DATA_END - ovl16_DATA_START);
        ovl16_BSS_START = .;
        build/asm/stagprog/data/ovl16_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl16.s.o(.bss);
        ovl16_BSS_END = .;
        ovl16_BSS_SIZE = ABSOLUTE(ovl16_BSS_END - ovl16_BSS_START);
    }
    __romPos += SIZEOF(.ovl16);
    __romPos = ALIGN(__romPos, 16);
    ovl16_ROM_END = __romPos;
    ovl16_VRAM_END = .;

    ovl17_ROM_START = __romPos;
    ovl17_VRAM = ADDR(.ovl17);
    .ovl17 0x800F2C50 : AT(ovl17_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl17_RODATA_START = .;
        build/asm/stagprog/data/ovl17_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl17.s.o(.rodata);
        ovl17_RODATA_END = .;
        ovl17_RODATA_SIZE = ABSOLUTE(ovl17_RODATA_END - ovl17_RODATA_START);
        ovl17_TEXT_START = .;
        build/asm/stagprog/ovl17.s.o(.text);
        ovl17_TEXT_END = .;
        ovl17_TEXT_SIZE = ABSOLUTE(ovl17_TEXT_END - ovl17_TEXT_START);
        ovl17_DATA_START = .;
        build/asm/stagprog/data/ovl17_header.rodata.s.o(.data);
        build/asm/stagprog/ovl17.s.o(.data);
        build/asm/stagprog/data/ovl17_data.data.s.o(.data);
        ovl17_DATA_END = .;
        ovl17_DATA_SIZE = ABSOLUTE(ovl17_DATA_END - ovl17_DATA_START);
        ovl17_BSS_START = .;
        build/asm/stagprog/data/ovl17_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl17.s.o(.bss);
        ovl17_BSS_END = .;
        ovl17_BSS_SIZE = ABSOLUTE(ovl17_BSS_END - ovl17_BSS_START);
    }
    __romPos += SIZEOF(.ovl17);
    __romPos = ALIGN(__romPos, 16);
    ovl17_ROM_END = __romPos;
    ovl17_VRAM_END = .;

    ovl18_ROM_START = __romPos;
    ovl18_VRAM = ADDR(.ovl18);
    .ovl18 0x800F2C50 : AT(ovl18_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl18_RODATA_START = .;
        build/asm/stagprog/data/ovl18_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl18.s.o(.rodata);
        ovl18_RODATA_END = .;
        ovl18_RODATA_SIZE = ABSOLUTE(ovl18_RODATA_END - ovl18_RODATA_START);
        ovl18_TEXT_START = .;
        build/asm/stagprog/ovl18.s.o(.text);
        ovl18_TEXT_END = .;
        ovl18_TEXT_SIZE = ABSOLUTE(ovl18_TEXT_END - ovl18_TEXT_START);
        ovl18_DATA_START = .;
        build/asm/stagprog/data/ovl18_header.rodata.s.o(.data);
        build/asm/stagprog/ovl18.s.o(.data);
        build/asm/stagprog/data/ovl18_data.data.s.o(.data);
        ovl18_DATA_END = .;
        ovl18_DATA_SIZE = ABSOLUTE(ovl18_DATA_END - ovl18_DATA_START);
        ovl18_BSS_START = .;
        build/asm/stagprog/data/ovl18_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl18.s.o(.bss);
        ovl18_BSS_END = .;
        ovl18_BSS_SIZE = ABSOLUTE(ovl18_BSS_END - ovl18_BSS_START);
    }
    __romPos += SIZEOF(.ovl18);
    __romPos = ALIGN(__romPos, 16);
    ovl18_ROM_END = __romPos;
    ovl18_VRAM_END = .;

    ovl19_ROM_START = __romPos;
    ovl19_VRAM = ADDR(.ovl19);
    .ovl19 0x800F2C50 : AT(ovl19_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl19_RODATA_START = .;
        build/asm/stagprog/data/ovl19_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl19.s.o(.rodata);
        ovl19_RODATA_END = .;
        ovl19_RODATA_SIZE = ABSOLUTE(ovl19_RODATA_END - ovl19_RODATA_START);
        ovl19_TEXT_START = .;
        build/asm/stagprog/ovl19.s.o(.text);
        ovl19_TEXT_END = .;
        ovl19_TEXT_SIZE = ABSOLUTE(ovl19_TEXT_END - ovl19_TEXT_START);
        ovl19_DATA_START = .;
        build/asm/stagprog/data/ovl19_header.rodata.s.o(.data);
        build/asm/stagprog/ovl19.s.o(.data);
        build/asm/stagprog/data/ovl19_data.data.s.o(.data);
        ovl19_DATA_END = .;
        ovl19_DATA_SIZE = ABSOLUTE(ovl19_DATA_END - ovl19_DATA_START);
        ovl19_BSS_START = .;
        build/asm/stagprog/data/ovl19_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl19.s.o(.bss);
        ovl19_BSS_END = .;
        ovl19_BSS_SIZE = ABSOLUTE(ovl19_BSS_END - ovl19_BSS_START);
    }
    __romPos += SIZEOF(.ovl19);
    __romPos = ALIGN(__romPos, 16);
    ovl19_ROM_END = __romPos;
    ovl19_VRAM_END = .;

    ovl20_ROM_START = __romPos;
    ovl20_VRAM = ADDR(.ovl20);
    .ovl20 0x800F2C50 : AT(ovl20_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl20_RODATA_START = .;
        build/asm/stagprog/data/ovl20_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl20.s.o(.rodata);
        ovl20_RODATA_END = .;
        ovl20_RODATA_SIZE = ABSOLUTE(ovl20_RODATA_END - ovl20_RODATA_START);
        ovl20_TEXT_START = .;
        build/asm/stagprog/ovl20.s.o(.text);
        ovl20_TEXT_END = .;
        ovl20_TEXT_SIZE = ABSOLUTE(ovl20_TEXT_END - ovl20_TEXT_START);
        ovl20_DATA_START = .;
        build/asm/stagprog/data/ovl20_header.rodata.s.o(.data);
        build/asm/stagprog/ovl20.s.o(.data);
        build/asm/stagprog/data/ovl20_data.data.s.o(.data);
        ovl20_DATA_END = .;
        ovl20_DATA_SIZE = ABSOLUTE(ovl20_DATA_END - ovl20_DATA_START);
        ovl20_BSS_START = .;
        build/asm/stagprog/data/ovl20_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl20.s.o(.bss);
        ovl20_BSS_END = .;
        ovl20_BSS_SIZE = ABSOLUTE(ovl20_BSS_END - ovl20_BSS_START);
    }
    __romPos += SIZEOF(.ovl20);
    __romPos = ALIGN(__romPos, 16);
    ovl20_ROM_END = __romPos;
    ovl20_VRAM_END = .;

    ovl21_ROM_START = __romPos;
    ovl21_VRAM = ADDR(.ovl21);
    .ovl21 0x800F2C50 : AT(ovl21_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl21_RODATA_START = .;
        build/asm/stagprog/data/ovl21_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl21.s.o(.rodata);
        ovl21_RODATA_END = .;
        ovl21_RODATA_SIZE = ABSOLUTE(ovl21_RODATA_END - ovl21_RODATA_START);
        ovl21_TEXT_START = .;
        build/asm/stagprog/ovl21.s.o(.text);
        ovl21_TEXT_END = .;
        ovl21_TEXT_SIZE = ABSOLUTE(ovl21_TEXT_END - ovl21_TEXT_START);
        ovl21_DATA_START = .;
        build/asm/stagprog/data/ovl21_header.rodata.s.o(.data);
        build/asm/stagprog/ovl21.s.o(.data);
        build/asm/stagprog/data/ovl21_data.data.s.o(.data);
        ovl21_DATA_END = .;
        ovl21_DATA_SIZE = ABSOLUTE(ovl21_DATA_END - ovl21_DATA_START);
        ovl21_BSS_START = .;
        build/asm/stagprog/data/ovl21_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl21.s.o(.bss);
        ovl21_BSS_END = .;
        ovl21_BSS_SIZE = ABSOLUTE(ovl21_BSS_END - ovl21_BSS_START);
    }
    __romPos += SIZEOF(.ovl21);
    __romPos = ALIGN(__romPos, 16);
    ovl21_ROM_END = __romPos;
    ovl21_VRAM_END = .;

    ovl22_ROM_START = __romPos;
    ovl22_VRAM = ADDR(.ovl22);
    .ovl22 0x800F2C50 : AT(ovl22_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl22_RODATA_START = .;
        build/asm/stagprog/data/ovl22_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl22.s.o(.rodata);
        ovl22_RODATA_END = .;
        ovl22_RODATA_SIZE = ABSOLUTE(ovl22_RODATA_END - ovl22_RODATA_START);
        ovl22_TEXT_START = .;
        build/asm/stagprog/ovl22.s.o(.text);
        ovl22_TEXT_END = .;
        ovl22_TEXT_SIZE = ABSOLUTE(ovl22_TEXT_END - ovl22_TEXT_START);
        ovl22_DATA_START = .;
        build/asm/stagprog/data/ovl22_header.rodata.s.o(.data);
        build/asm/stagprog/ovl22.s.o(.data);
        build/asm/stagprog/data/ovl22_data.data.s.o(.data);
        ovl22_DATA_END = .;
        ovl22_DATA_SIZE = ABSOLUTE(ovl22_DATA_END - ovl22_DATA_START);
        ovl22_BSS_START = .;
        build/asm/stagprog/data/ovl22_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl22.s.o(.bss);
        ovl22_BSS_END = .;
        ovl22_BSS_SIZE = ABSOLUTE(ovl22_BSS_END - ovl22_BSS_START);
    }
    __romPos += SIZEOF(.ovl22);
    __romPos = ALIGN(__romPos, 16);
    ovl22_ROM_END = __romPos;
    ovl22_VRAM_END = .;

    ovl23_ROM_START = __romPos;
    ovl23_VRAM = ADDR(.ovl23);
    .ovl23 0x800F2C50 : AT(ovl23_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl23_RODATA_START = .;
        build/asm/stagprog/data/ovl23_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl23.s.o(.rodata);
        ovl23_RODATA_END = .;
        ovl23_RODATA_SIZE = ABSOLUTE(ovl23_RODATA_END - ovl23_RODATA_START);
        ovl23_TEXT_START = .;
        build/asm/stagprog/ovl23.s.o(.text);
        ovl23_TEXT_END = .;
        ovl23_TEXT_SIZE = ABSOLUTE(ovl23_TEXT_END - ovl23_TEXT_START);
        ovl23_DATA_START = .;
        build/asm/stagprog/data/ovl23_header.rodata.s.o(.data);
        build/asm/stagprog/ovl23.s.o(.data);
        build/asm/stagprog/data/ovl23_data.data.s.o(.data);
        ovl23_DATA_END = .;
        ovl23_DATA_SIZE = ABSOLUTE(ovl23_DATA_END - ovl23_DATA_START);
        ovl23_BSS_START = .;
        build/asm/stagprog/data/ovl23_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl23.s.o(.bss);
        ovl23_BSS_END = .;
        ovl23_BSS_SIZE = ABSOLUTE(ovl23_BSS_END - ovl23_BSS_START);
    }
    __romPos += SIZEOF(.ovl23);
    __romPos = ALIGN(__romPos, 16);
    ovl23_ROM_END = __romPos;
    ovl23_VRAM_END = .;

    ovl24_ROM_START = __romPos;
    ovl24_VRAM = ADDR(.ovl24);
    .ovl24 0x800F2C50 : AT(ovl24_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl24_RODATA_START = .;
        build/asm/stagprog/data/ovl24_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl24.s.o(.rodata);
        ovl24_RODATA_END = .;
        ovl24_RODATA_SIZE = ABSOLUTE(ovl24_RODATA_END - ovl24_RODATA_START);
        ovl24_TEXT_START = .;
        build/asm/stagprog/ovl24.s.o(.text);
        ovl24_TEXT_END = .;
        ovl24_TEXT_SIZE = ABSOLUTE(ovl24_TEXT_END - ovl24_TEXT_START);
        ovl24_DATA_START = .;
        build/asm/stagprog/data/ovl24_header.rodata.s.o(.data);
        build/asm/stagprog/ovl24.s.o(.data);
        build/asm/stagprog/data/ovl24_data.data.s.o(.data);
        ovl24_DATA_END = .;
        ovl24_DATA_SIZE = ABSOLUTE(ovl24_DATA_END - ovl24_DATA_START);
        ovl24_BSS_START = .;
        build/asm/stagprog/data/ovl24_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl24.s.o(.bss);
        ovl24_BSS_END = .;
        ovl24_BSS_SIZE = ABSOLUTE(ovl24_BSS_END - ovl24_BSS_START);
    }
    __romPos += SIZEOF(.ovl24);
    __romPos = ALIGN(__romPos, 16);
    ovl24_ROM_END = __romPos;
    ovl24_VRAM_END = .;

    ovl25_ROM_START = __romPos;
    ovl25_VRAM = ADDR(.ovl25);
    .ovl25 0x800F2C50 : AT(ovl25_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl25_RODATA_START = .;
        build/asm/stagprog/data/ovl25_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl25.s.o(.rodata);
        ovl25_RODATA_END = .;
        ovl25_RODATA_SIZE = ABSOLUTE(ovl25_RODATA_END - ovl25_RODATA_START);
        ovl25_TEXT_START = .;
        build/asm/stagprog/ovl25.s.o(.text);
        ovl25_TEXT_END = .;
        ovl25_TEXT_SIZE = ABSOLUTE(ovl25_TEXT_END - ovl25_TEXT_START);
        ovl25_DATA_START = .;
        build/asm/stagprog/data/ovl25_header.rodata.s.o(.data);
        build/asm/stagprog/ovl25.s.o(.data);
        build/asm/stagprog/data/ovl25_data.data.s.o(.data);
        ovl25_DATA_END = .;
        ovl25_DATA_SIZE = ABSOLUTE(ovl25_DATA_END - ovl25_DATA_START);
        ovl25_BSS_START = .;
        build/asm/stagprog/data/ovl25_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl25.s.o(.bss);
        ovl25_BSS_END = .;
        ovl25_BSS_SIZE = ABSOLUTE(ovl25_BSS_END - ovl25_BSS_START);
    }
    __romPos += SIZEOF(.ovl25);
    __romPos = ALIGN(__romPos, 16);
    ovl25_ROM_END = __romPos;
    ovl25_VRAM_END = .;

    ovl26_ROM_START = __romPos;
    ovl26_VRAM = ADDR(.ovl26);
    .ovl26 0x800F2C50 : AT(ovl26_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl26_RODATA_START = .;
        build/asm/stagprog/data/ovl26_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl26.s.o(.rodata);
        ovl26_RODATA_END = .;
        ovl26_RODATA_SIZE = ABSOLUTE(ovl26_RODATA_END - ovl26_RODATA_START);
        ovl26_TEXT_START = .;
        build/asm/stagprog/ovl26.s.o(.text);
        ovl26_TEXT_END = .;
        ovl26_TEXT_SIZE = ABSOLUTE(ovl26_TEXT_END - ovl26_TEXT_START);
        ovl26_DATA_START = .;
        build/asm/stagprog/data/ovl26_header.rodata.s.o(.data);
        build/asm/stagprog/ovl26.s.o(.data);
        build/asm/stagprog/data/ovl26_data.data.s.o(.data);
        ovl26_DATA_END = .;
        ovl26_DATA_SIZE = ABSOLUTE(ovl26_DATA_END - ovl26_DATA_START);
        ovl26_BSS_START = .;
        build/asm/stagprog/data/ovl26_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl26.s.o(.bss);
        ovl26_BSS_END = .;
        ovl26_BSS_SIZE = ABSOLUTE(ovl26_BSS_END - ovl26_BSS_START);
    }
    __romPos += SIZEOF(.ovl26);
    __romPos = ALIGN(__romPos, 16);
    ovl26_ROM_END = __romPos;
    ovl26_VRAM_END = .;

    ovl27_ROM_START = __romPos;
    ovl27_VRAM = ADDR(.ovl27);
    .ovl27 0x800F2C50 : AT(ovl27_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl27_RODATA_START = .;
        build/asm/stagprog/data/ovl27_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl27.s.o(.rodata);
        ovl27_RODATA_END = .;
        ovl27_RODATA_SIZE = ABSOLUTE(ovl27_RODATA_END - ovl27_RODATA_START);
        ovl27_TEXT_START = .;
        build/asm/stagprog/ovl27.s.o(.text);
        ovl27_TEXT_END = .;
        ovl27_TEXT_SIZE = ABSOLUTE(ovl27_TEXT_END - ovl27_TEXT_START);
        ovl27_DATA_START = .;
        build/asm/stagprog/data/ovl27_header.rodata.s.o(.data);
        build/asm/stagprog/ovl27.s.o(.data);
        build/asm/stagprog/data/ovl27_data.data.s.o(.data);
        ovl27_DATA_END = .;
        ovl27_DATA_SIZE = ABSOLUTE(ovl27_DATA_END - ovl27_DATA_START);
        ovl27_BSS_START = .;
        build/asm/stagprog/data/ovl27_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl27.s.o(.bss);
        ovl27_BSS_END = .;
        ovl27_BSS_SIZE = ABSOLUTE(ovl27_BSS_END - ovl27_BSS_START);
    }
    __romPos += SIZEOF(.ovl27);
    __romPos = ALIGN(__romPos, 16);
    ovl27_ROM_END = __romPos;
    ovl27_VRAM_END = .;

    ovl28_ROM_START = __romPos;
    ovl28_VRAM = ADDR(.ovl28);
    .ovl28 0x800F2C50 : AT(ovl28_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl28_RODATA_START = .;
        build/asm/stagprog/data/ovl28_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl28.s.o(.rodata);
        ovl28_RODATA_END = .;
        ovl28_RODATA_SIZE = ABSOLUTE(ovl28_RODATA_END - ovl28_RODATA_START);
        ovl28_TEXT_START = .;
        build/asm/stagprog/ovl28.s.o(.text);
        ovl28_TEXT_END = .;
        ovl28_TEXT_SIZE = ABSOLUTE(ovl28_TEXT_END - ovl28_TEXT_START);
        ovl28_DATA_START = .;
        build/asm/stagprog/data/ovl28_header.rodata.s.o(.data);
        build/asm/stagprog/ovl28.s.o(.data);
        build/asm/stagprog/data/ovl28_data.data.s.o(.data);
        ovl28_DATA_END = .;
        ovl28_DATA_SIZE = ABSOLUTE(ovl28_DATA_END - ovl28_DATA_START);
        ovl28_BSS_START = .;
        build/asm/stagprog/data/ovl28_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl28.s.o(.bss);
        ovl28_BSS_END = .;
        ovl28_BSS_SIZE = ABSOLUTE(ovl28_BSS_END - ovl28_BSS_START);
    }
    __romPos += SIZEOF(.ovl28);
    __romPos = ALIGN(__romPos, 16);
    ovl28_ROM_END = __romPos;
    ovl28_VRAM_END = .;

    ovl29_ROM_START = __romPos;
    ovl29_VRAM = ADDR(.ovl29);
    .ovl29 0x800F2C50 : AT(ovl29_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl29_RODATA_START = .;
        build/asm/stagprog/data/ovl29_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl29.s.o(.rodata);
        ovl29_RODATA_END = .;
        ovl29_RODATA_SIZE = ABSOLUTE(ovl29_RODATA_END - ovl29_RODATA_START);
        ovl29_TEXT_START = .;
        build/asm/stagprog/ovl29.s.o(.text);
        ovl29_TEXT_END = .;
        ovl29_TEXT_SIZE = ABSOLUTE(ovl29_TEXT_END - ovl29_TEXT_START);
        ovl29_DATA_START = .;
        build/asm/stagprog/data/ovl29_header.rodata.s.o(.data);
        build/asm/stagprog/ovl29.s.o(.data);
        build/asm/stagprog/data/ovl29_data.data.s.o(.data);
        ovl29_DATA_END = .;
        ovl29_DATA_SIZE = ABSOLUTE(ovl29_DATA_END - ovl29_DATA_START);
        ovl29_BSS_START = .;
        build/asm/stagprog/data/ovl29_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl29.s.o(.bss);
        ovl29_BSS_END = .;
        ovl29_BSS_SIZE = ABSOLUTE(ovl29_BSS_END - ovl29_BSS_START);
    }
    __romPos += SIZEOF(.ovl29);
    __romPos = ALIGN(__romPos, 16);
    ovl29_ROM_END = __romPos;
    ovl29_VRAM_END = .;

    ovl30_ROM_START = __romPos;
    ovl30_VRAM = ADDR(.ovl30);
    .ovl30 0x800F2C50 : AT(ovl30_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl30_RODATA_START = .;
        build/asm/stagprog/data/ovl30_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl30.s.o(.rodata);
        ovl30_RODATA_END = .;
        ovl30_RODATA_SIZE = ABSOLUTE(ovl30_RODATA_END - ovl30_RODATA_START);
        ovl30_TEXT_START = .;
        build/asm/stagprog/ovl30.s.o(.text);
        ovl30_TEXT_END = .;
        ovl30_TEXT_SIZE = ABSOLUTE(ovl30_TEXT_END - ovl30_TEXT_START);
        ovl30_DATA_START = .;
        build/asm/stagprog/data/ovl30_header.rodata.s.o(.data);
        build/asm/stagprog/ovl30.s.o(.data);
        build/asm/stagprog/data/ovl30_data.data.s.o(.data);
        ovl30_DATA_END = .;
        ovl30_DATA_SIZE = ABSOLUTE(ovl30_DATA_END - ovl30_DATA_START);
        ovl30_BSS_START = .;
        build/asm/stagprog/data/ovl30_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl30.s.o(.bss);
        ovl30_BSS_END = .;
        ovl30_BSS_SIZE = ABSOLUTE(ovl30_BSS_END - ovl30_BSS_START);
    }
    __romPos += SIZEOF(.ovl30);
    __romPos = ALIGN(__romPos, 16);
    ovl30_ROM_END = __romPos;
    ovl30_VRAM_END = .;

    ovl31_ROM_START = __romPos;
    ovl31_VRAM = ADDR(.ovl31);
    .ovl31 0x800F2C50 : AT(ovl31_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl31_RODATA_START = .;
        build/asm/stagprog/data/ovl31_header.rodata.s.o(.rodata);
        build/asm/stagprog/ovl31.s.o(.rodata);
        ovl31_RODATA_END = .;
        ovl31_RODATA_SIZE = ABSOLUTE(ovl31_RODATA_END - ovl31_RODATA_START);
        ovl31_TEXT_START = .;
        build/asm/stagprog/ovl31.s.o(.text);
        ovl31_TEXT_END = .;
        ovl31_TEXT_SIZE = ABSOLUTE(ovl31_TEXT_END - ovl31_TEXT_START);
        ovl31_DATA_START = .;
        build/asm/stagprog/data/ovl31_header.rodata.s.o(.data);
        build/asm/stagprog/ovl31.s.o(.data);
        build/asm/stagprog/data/ovl31_data.data.s.o(.data);
        ovl31_DATA_END = .;
        ovl31_DATA_SIZE = ABSOLUTE(ovl31_DATA_END - ovl31_DATA_START);
        ovl31_BSS_START = .;
        build/asm/stagprog/data/ovl31_header.rodata.s.o(.bss);
        build/asm/stagprog/ovl31.s.o(.bss);
        ovl31_BSS_END = .;
        ovl31_BSS_SIZE = ABSOLUTE(ovl31_BSS_END - ovl31_BSS_START);
    }
    __romPos += SIZEOF(.ovl31);
    __romPos = ALIGN(__romPos, 16);
    ovl31_ROM_END = __romPos;
    ovl31_VRAM_END = .;

    ovl32_ROM_START = __romPos;
    ovl32_VRAM = ADDR(.ovl32);
    .ovl32 : AT(ovl32_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        ovl32_DATA_START = .;
        build/assets/stagprog/ovl32.bin.o(.data);
        ovl32_DATA_END = .;
        ovl32_DATA_SIZE = ABSOLUTE(ovl32_DATA_END - ovl32_DATA_START);
    }
    __romPos += SIZEOF(.ovl32);
    ovl32_ROM_END = __romPos;
    ovl32_VRAM_END = .;

    /DISCARD/ :
    {
        *(*);
    }
}
