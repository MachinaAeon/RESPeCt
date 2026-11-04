/* Default-off synchronous raw-TIM staging. No CD timing or gameplay policy. */
#ifndef MODIO_H
#define MODIO_H
#include <stdint.h>
#include <stddef.h>
typedef struct { uint32_t version,base,capacity,mask; uint64_t signature; } ModIoState;
uint32_t modio_original_size(uint32_t table,int index);
int modio_tim_valid(const void *data,size_t bytes);
int modio_boot(uint32_t base,uint32_t capacity);
uint32_t modio_capacity(void);
int modio_register(uint32_t table,uint32_t index,uint32_t lba,uint32_t sectors);
int modio_active(void);
/* 1: original destination;2: exclusive lease;0: refuse before writes. */
int modio_acquire(uint32_t table,uint32_t index,uint32_t original,uint32_t *dest);
int modio_release(uint32_t table,uint32_t index);
int modio_busy(void);
int modio_check_position(const uint8_t position[3],uint32_t sectors,uint32_t dest);
int modio_check_read(uint32_t lba,uint32_t sectors,uint32_t dest);
void modio_state_get(ModIoState *state);
int modio_state_valid(const ModIoState *state,size_t extended_bytes);
/* Reapply immutable static mods, preserving this validated mutable span. */
int modio_static_copy(void *dest,const void *source,size_t bytes);
#endif
