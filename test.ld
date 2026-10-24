SECTIONS
{
    HIDDEN(__romPos = 0);
    _gp = 0x80062D1C;
    test_ROM_START = __romPos;
    test_VRAM = ADDR(.test);
    .test 0x8006C118 : AT(test_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        test_RODATA_START = .;
        build/asm/test/data/test_header.rodata.s.o(.rodata);
        build/asm/test/test.s.o(.rodata);
        test_RODATA_END = .;
        test_RODATA_SIZE = ABSOLUTE(test_RODATA_END - test_RODATA_START);
        test_TEXT_START = .;
        build/asm/test/test.s.o(.text);
        test_TEXT_END = .;
        test_TEXT_SIZE = ABSOLUTE(test_TEXT_END - test_TEXT_START);
        test_DATA_START = .;
        build/asm/test/data/test_header.rodata.s.o(.data);
        build/asm/test/test.s.o(.data);
        build/asm/test/data/test_data.data.s.o(.data);
        test_DATA_END = .;
        test_DATA_SIZE = ABSOLUTE(test_DATA_END - test_DATA_START);
        test_BSS_START = .;
        build/asm/test/data/test_header.rodata.s.o(.bss);
        build/asm/test/test.s.o(.bss);
        test_BSS_END = .;
        test_BSS_SIZE = ABSOLUTE(test_BSS_END - test_BSS_START);
    }
    __romPos += SIZEOF(.test);
    __romPos = ALIGN(__romPos, 16);
    test_ROM_END = __romPos;
    test_VRAM_END = .;

    /DISCARD/ :
    {
        *(*);
    }
}
