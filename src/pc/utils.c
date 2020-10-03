#ifdef TARGET_XBOX

#include <stdlib.h>
#include <string.h>

void bcopy(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
}

void bzero(void *dst, size_t len) {
    memset(dst, 0, len);
}

#endif
