#ifndef _PC_UTILS_H
#define _PC_UTILS_H

#define SAVE_FILE   "sm64_save_file.bin"
#define CONFIG_FILE "sm64config.txt"

const char *get_user_path(void);
const char *get_config_filename(void);
const char *get_save_filename(void);

#endif // _PC_UTILS_H
