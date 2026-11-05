/* Verified USA pickup identities and complete native stream guards.
 * Metadata only: no message text or image data. PC canvas is 512 x 512.
 * PC0045B9B0 formats PIC's hex digits from the original item argument;
 * PS1 n_pickup_Step builds prefix/name/controls in800A88C8. Each listed
 * identity was also checked against the owned reference picture content.
 * The45 distinct matching pictures are explicit, not a numeric range.
 * Fifteen placeholder/empty-identity/mismatched pictures remain excluded.
 * PIC43 matches Andy's diary; its duplicate PIC46 wrongly labels Young
 * Man's Diary and is excluded. Guard the entire native stream, including
 * the measured native wait mutation, without changing original controls. */
#ifndef MODFS_PC_PICKUPS_H
#define MODFS_PC_PICKUPS_H
#define MODFS_PC_AUTOMATIC_PICKUPS 1

typedef struct { const char *pc; uint32_t id, words, crc; } PcPickupProfile;
static const PcPickupProfile modfs_pc_pickups[] = {
    {"ITEM/PIC/01.BMP", 1u, 31u, 0x6702FD76u},
    {"ITEM/PIC/02.BMP", 2u, 31u, 0xFCA7B119u},
    {"ITEM/PIC/03.BMP", 3u, 31u, 0x9A1A186Cu},
    {"ITEM/PIC/04.BMP", 4u, 29u, 0x1D39E8C9u},
    {"ITEM/PIC/05.BMP", 5u, 28u, 0x834B390Fu},
    {"ITEM/PIC/08.BMP", 8u, 33u, 0xCE52924Au},
    {"ITEM/PIC/0B.BMP", 11u, 37u, 0x9B4E386Fu},
    {"ITEM/PIC/0C.BMP", 12u, 35u, 0x52009697u},
    {"ITEM/PIC/0D.BMP", 13u, 34u, 0x85E88093u},
    {"ITEM/PIC/0E.BMP", 14u, 33u, 0x297E590Bu},
    {"ITEM/PIC/0F.BMP", 15u, 36u, 0xC735A2B2u},
    {"ITEM/PIC/14.BMP", 20u, 37u, 0xB967BFF9u},
    {"ITEM/PIC/15.BMP", 21u, 32u, 0xAE91D59Cu},
    {"ITEM/PIC/16.BMP", 22u, 30u, 0x95B56DC0u},
    {"ITEM/PIC/17.BMP", 23u, 31u, 0xF5A9E55Bu},
    {"ITEM/PIC/20.BMP", 32u, 34u, 0xBB38C67Cu},
    {"ITEM/PIC/21.BMP", 33u, 32u, 0x4728EB66u},
    {"ITEM/PIC/22.BMP", 34u, 39u, 0xEE90C0F2u},
    {"ITEM/PIC/24.BMP", 36u, 32u, 0x5BED583Au},
    {"ITEM/PIC/25.BMP", 37u, 35u, 0xFE04ADA7u},
    {"ITEM/PIC/26.BMP", 38u, 31u, 0x5587C8AAu},
    {"ITEM/PIC/27.BMP", 39u, 33u, 0x26A10E4Fu},
    {"ITEM/PIC/29.BMP", 41u, 37u, 0x9645E781u},
    {"ITEM/PIC/2B.BMP", 43u, 26u, 0x4667D466u},
    {"ITEM/PIC/2C.BMP", 44u, 33u, 0x0E034CA3u},
    {"ITEM/PIC/2D.BMP", 45u, 32u, 0xEDB7E32Au},
    {"ITEM/PIC/2E.BMP", 46u, 35u, 0xC0663553u},
    {"ITEM/PIC/2F.BMP", 47u, 30u, 0x8665D2BEu},
    {"ITEM/PIC/30.BMP", 48u, 36u, 0xA8E3140Du},
    {"ITEM/PIC/34.BMP", 52u, 37u, 0x18F86CFFu},
    {"ITEM/PIC/35.BMP", 53u, 32u, 0xA78F4219u},
    {"ITEM/PIC/36.BMP", 54u, 29u, 0x66F2F053u},
    {"ITEM/PIC/37.BMP", 55u, 39u, 0xC5DBCE78u},
    {"ITEM/PIC/40.BMP", 64u, 44u, 0xC88BE6A5u},
    {"ITEM/PIC/41.BMP", 65u, 47u, 0x7985C727u},
    {"ITEM/PIC/42.BMP", 66u, 37u, 0x5BAFC51Fu},
    {"ITEM/PIC/43.BMP", 67u, 42u, 0x09EEF452u},
    {"ITEM/PIC/44.BMP", 68u, 37u, 0x350B6323u},
    {"ITEM/PIC/45.BMP", 69u, 46u, 0x0E07D3D5u},
    {"ITEM/PIC/47.BMP", 71u, 34u, 0x16C8366Bu},
    {"ITEM/PIC/48.BMP", 72u, 46u, 0x92FCC7B2u},
    {"ITEM/PIC/49.BMP", 73u, 44u, 0xDB519A9Cu},
    {"ITEM/PIC/4A.BMP", 74u, 49u, 0x81554731u},
    {"ITEM/PIC/4B.BMP", 75u, 42u, 0x49970058u},
    {"ITEM/PIC/4C.BMP", 76u, 54u, 0x17B8E529u},
};
#endif
