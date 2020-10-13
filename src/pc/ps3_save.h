#ifndef _PS3_SAVE_H
#define _PS3_SAVE_H

#include <stdint.h>
#include <stdbool.h>

#define PS3_SAVE_SIZE 1024
#define PS3_CFG_OFS 512

bool ps3_save_init(void);
void ps3_save_shutdown(void);

bool ps3_save_write(const void *data, const uint32_t ofs, const uint32_t size);
bool ps3_save_read(void *data, const uint32_t ofs, const uint32_t size);

bool ps3_save_save(void);
bool ps3_save_load(void);

#endif // _PS3_SAVE_H
