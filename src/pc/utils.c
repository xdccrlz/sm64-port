#ifdef TARGET_XBOX

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <nxdk/mount.h>
#include <winapi/fileapi.h>

#include "utils.h"

void bcopy(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
}

void bzero(void *dst, size_t len) {
    memset(dst, 0, len);
}

const char *get_user_path(void) {
    static char path[32] = { 0 };
    if (!path[0]) {
        if (!nxIsDriveMounted('E'))
            nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
        snprintf(path, sizeof(path), "E:\\UDATA\\%08x", XBE_TITLE_ID);
        CreateDirectoryA(path, NULL);
        strncat(path, "\\000000000000", sizeof(path) - 1);
        CreateDirectoryA(path, NULL);
        strncat(path, "\\", sizeof(path) - 1);
    }
    return path;
}

#else

const char *get_user_path(void) {
    return "";
}

#endif

const char *get_config_filename(void) {
    static char path[64] = { 0 };
    if (!path[0]) snprintf(path, sizeof(path), "%s%s", get_user_path(), CONFIG_FILE);
    return path;
}

const char *get_save_filename(void) {
    static char path[64] = { 0 };
    if (!path[0]) snprintf(path, sizeof(path), "%s%s", get_user_path(), SAVE_FILE);
    return path;
}
