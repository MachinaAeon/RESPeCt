/* The BIOS call stubs (libapi / libc / libcard, 0x80038F88-0x8003A268):
 * each loads the call number into $t1 and jumps to the A0 / B0 / C0
 * vector with $ra untouched, so the call returns straight to the
 * stub's caller. Named by vector and number. */
#include "poll.h"

/* 80038F88 */
void n_api_A0_70(void) { R[10] = 0xA0u; R[9] = 0x70u; rt_call(0xA0u); }
/* 80038F98 */
void n_api_A0_AB(void) { R[10] = 0xA0u; R[9] = 0xABu; rt_call(0xA0u); }
/* 80038FA8 */
void n_api_A0_AC(void) { R[10] = 0xA0u; R[9] = 0xACu; rt_call(0xA0u); }
/* 80038FB8 */
void n_api_A0_AD(void) { R[10] = 0xA0u; R[9] = 0xADu; rt_call(0xA0u); }
/* 80038FC8 */
void n_api_B0_5D(void) { R[10] = 0xB0u; R[9] = 0x5Du; rt_call(0xB0u); }
/* 80039138 */
void n_api_B0_4A(void) { R[10] = 0xB0u; R[9] = 0x4Au; rt_call(0xB0u); }
/* 80039148 */
void n_api_B0_4B(void) { R[10] = 0xB0u; R[9] = 0x4Bu; rt_call(0xB0u); }
/* 80039800 */
void n_api_B0_3F(void) { R[10] = 0xB0u; R[9] = 0x3Fu; rt_call(0xB0u); }
/* 80039818 */
void n_api_A0_13(void) { R[10] = 0xA0u; R[9] = 0x13u; rt_call(0xA0u); }
/* 80039828 */
void n_api_A0_15(void) { R[10] = 0xA0u; R[9] = 0x15u; rt_call(0xA0u); }
/* 80039838 */
void n_api_A0_18(void) { R[10] = 0xA0u; R[9] = 0x18u; rt_call(0xA0u); }
/* 80039848 */
void n_api_A0_19(void) { R[10] = 0xA0u; R[9] = 0x19u; rt_call(0xA0u); }
/* 80039868 */
void n_api_A0_27(void) { R[10] = 0xA0u; R[9] = 0x27u; rt_call(0xA0u); }
/* 80039878 */
void n_api_A0_28(void) { R[10] = 0xA0u; R[9] = 0x28u; rt_call(0xA0u); }
/* 80039888 */
void n_api_A0_2A(void) { R[10] = 0xA0u; R[9] = 0x2Au; rt_call(0xA0u); }
/* 80039898 */
void n_api_A0_2B(void) { R[10] = 0xA0u; R[9] = 0x2Bu; rt_call(0xA0u); }
/* 800398A8 */
void n_api_A0_2F(void) { R[10] = 0xA0u; R[9] = 0x2Fu; rt_call(0xA0u); }
/* 800398B8 */
void n_api_A0_30(void) { R[10] = 0xA0u; R[9] = 0x30u; rt_call(0xA0u); }
/* 800398C8 */
void n_api_A0_3F(void) { R[10] = 0xA0u; R[9] = 0x3Fu; rt_call(0xA0u); }
/* 800398D8 */
void n_api_A0_39(void) { R[10] = 0xA0u; R[9] = 0x39u; rt_call(0xA0u); }
/* 800398E8 */
void n_api_A0_44(void) { R[10] = 0xA0u; R[9] = 0x44u; rt_call(0xA0u); }
/* 800398F8 */
void n_api_A0_49(void) { R[10] = 0xA0u; R[9] = 0x49u; rt_call(0xA0u); }
/* 80039910 */
void n_api_A0_72(void) { R[10] = 0xA0u; R[9] = 0x72u; rt_call(0xA0u); }
/* 80039928 */
void n_api_A0_9F(void) { R[10] = 0xA0u; R[9] = 0x9Fu; rt_call(0xA0u); }
/* 80039938 */
void n_api_B0_07(void) { R[10] = 0xB0u; R[9] = 0x7u; rt_call(0xB0u); }
/* 80039948 */
void n_api_B0_08(void) { R[10] = 0xB0u; R[9] = 0x8u; rt_call(0xB0u); }
/* 80039958 */
void n_api_B0_09(void) { R[10] = 0xB0u; R[9] = 0x9u; rt_call(0xB0u); }
/* 80039968 */
void n_api_B0_0A(void) { R[10] = 0xB0u; R[9] = 0xAu; rt_call(0xB0u); }
/* 80039978 */
void n_api_B0_0B(void) { R[10] = 0xB0u; R[9] = 0xBu; rt_call(0xB0u); }
/* 80039988 */
void n_api_B0_0C(void) { R[10] = 0xB0u; R[9] = 0xCu; rt_call(0xB0u); }
/* 800399A8 */
void n_api_B0_0E(void) { R[10] = 0xB0u; R[9] = 0xEu; rt_call(0xB0u); }
/* 800399B8 */
void n_api_B0_0F(void) { R[10] = 0xB0u; R[9] = 0xFu; rt_call(0xB0u); }
/* 800399C8 */
void n_api_B0_10(void) { R[10] = 0xB0u; R[9] = 0x10u; rt_call(0xB0u); }
/* 800399D8 */
void n_api_B0_17(void) { R[10] = 0xB0u; R[9] = 0x17u; rt_call(0xB0u); }
/* 800399F8 */
void n_api_B0_19(void) { R[10] = 0xB0u; R[9] = 0x19u; rt_call(0xB0u); }
/* 80039A38 */
void n_api_B0_32(void) { R[10] = 0xB0u; R[9] = 0x32u; rt_call(0xB0u); }
/* 80039A48 */
void n_api_B0_33(void) { R[10] = 0xB0u; R[9] = 0x33u; rt_call(0xB0u); }
/* 80039A68 */
void n_api_B0_35(void) { R[10] = 0xB0u; R[9] = 0x35u; rt_call(0xB0u); }
/* 80039A78 */
void n_api_B0_36(void) { R[10] = 0xB0u; R[9] = 0x36u; rt_call(0xB0u); }
/* 80039A88 */
void n_api_B0_43(void) { R[10] = 0xB0u; R[9] = 0x43u; rt_call(0xB0u); }
/* 80039AA8 */
void n_api_B0_5B(void) { R[10] = 0xB0u; R[9] = 0x5Bu; rt_call(0xB0u); }
/* 80039AD8 */
void n_api_C0_0A(void) { R[10] = 0xC0u; R[9] = 0xAu; rt_call(0xC0u); }
/* 8003A258 */
void n_api_C0_02(void) { R[10] = 0xC0u; R[9] = 0x2u; rt_call(0xC0u); }
/* 8003A268 */
void n_api_C0_03(void) { R[10] = 0xC0u; R[9] = 0x3u; rt_call(0xC0u); }
