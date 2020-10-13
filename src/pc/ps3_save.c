#ifdef TARGET_PS3

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <sys/thread.h>
#include <sys/memory.h>
#include <sys/mutex.h>
#include <sysutil/sysutil.h>
#include <sysutil/save.h>

#include "macros.h"
#include "game/save_file.h"

#define SYS_MEMORY_CONTAINER_INVALID 0xFFFFFFFFU

#define SAVE_DIRNAME "SM64"
#define SAVE_FILENAME "SAVE"
#define SAVE_TITLE "Super Mario 64"
#define SAVE_SUBTITLE "it him"

#define SAVE_SIZE 2048
#define SAVE_SIZE_KB (SAVE_SIZE / 1024)
#define SAVE_MAX_FILES 3 // save + icon + pic
#define SAVE_MAX_DIRS 1

#define XMAX(a, b) (((a) > (b)) ? (a) : (b))

enum SaveMode {
    MODE_NONE,
    MODE_SAVE,
    MODE_LOAD,
    MODE_SAVE_DONE,
    MODE_LOAD_DONE,
};

static uint8_t save_data[SAVE_SIZE];
static uint32_t pending_save_size = 0;

static sys_ppu_thread_t save_thread;
static sys_mutex_t save_mutex;
static volatile int thread_mode = MODE_NONE;
static volatile int thread_ret = 0;

static bool init_done = false;

static inline void get_save_description(char *buf, int bufsize) {
    buf[0] = '\0';
    for (int i = 0; i < NUM_SAVE_FILES; ++i) {
        const int ret = snprintf(
            buf, bufsize,
            "FILE %c: %d\n",
            'A' + i,
            save_file_get_total_star_count(i, COURSE_MIN - 1, COURSE_MAX - 1)
        );
        if (ret < 0) break;
        buf += ret;
        bufsize -= ret;
    }
}

static void save_status_cb(sysSaveCallbackResult *res, sysSaveStatusIn *in, sysSaveStatusOut *out) {
    res->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    out->setParam = &in->getParam;

    if (thread_mode == MODE_SAVE) {
        out->recreateMode = SYS_SAVE_RECREATE_MODE_DELETE;

        if ((in->freeSpaceKB + in->sizeKB) < (SAVE_SIZE_KB + in->systemSizeKB)) {
            res->result = SYS_SAVE_CALLBACK_RESULT_NO_SPACE_LEFT;
            res->missingSpaceKB  = (SAVE_SIZE_KB + in->systemSizeKB) -
                (in->freeSpaceKB + in->sizeKB);
        }

        char detail[SYS_SAVE_MAX_DETAIL];
        get_save_description(detail, sizeof(detail));

        strncpy(in->getParam.title, SAVE_TITLE, SYS_SAVE_MAX_TITLE);
        strncpy(in->getParam.subtitle, SAVE_SUBTITLE, SYS_SAVE_MAX_SUBTITLE);
        strncpy(in->getParam.detail, detail, SYS_SAVE_MAX_DETAIL);
    } else if (thread_mode == MODE_LOAD) {
        out->recreateMode = SYS_SAVE_RECREATE_MODE_OVERWRITE_NOT_CORRUPTED;

        for (uint32_t i = 0; i < in->numFiles; i++) {
            switch (in->fileList[i].fileType) {
                case SYS_SAVE_FILETYPE_STANDARD_FILE:
                    pending_save_size = in->fileList[i].fileSize;
                    break;
                case SYS_SAVE_FILETYPE_CONTENT_ICON0:
                    // TODO
                    break;
                case SYS_SAVE_FILETYPE_CONTENT_PIC1:
                    // TODO
                    break;
                default:
                    break;
            }
        }

        if (pending_save_size == 0) {
            printf("save_status_cb: could not find valid save data\n");
            res->result = SYS_SAVE_CALLBACK_RESULT_CORRUPTED;
            return;
        }
    }
}

static void save_file_cb(sysSaveCallbackResult *res, sysSaveFileIn *in, sysSaveFileOut *out) {
    memset(out, 0, sizeof(*out));

    switch (thread_mode) {
        case MODE_SAVE:
            printf("save_file_cb: writing save data\n");
            out->fileOperation = SYS_SAVE_FILE_OPERATION_WRITE;
            thread_mode = MODE_SAVE_DONE;
            break;
        case MODE_LOAD:
            printf("save_file_cb: reading save data\n");
            out->fileOperation = SYS_SAVE_FILE_OPERATION_READ;
            thread_mode = MODE_LOAD_DONE;
            break;
        case MODE_SAVE_DONE:
            res->result = SYS_SAVE_CALLBACK_RESULT_DONE;
            return;
        case MODE_LOAD_DONE:
            if (in->previousOperationResultSize != SAVE_SIZE)
                res->result = SYS_SAVE_CALLBACK_RESULT_CORRUPTED;
            else
                res->result = SYS_SAVE_CALLBACK_RESULT_DONE;
            return;
        default:
            break;
    }

    out->filename = SAVE_FILENAME;
    out->fileType = SYS_SAVE_FILETYPE_STANDARD_FILE;
    out->size = SAVE_SIZE;
    out->bufferSize = SAVE_SIZE;
    out->buffer = save_data;

    res->result = SYS_SAVE_CALLBACK_RESULT_CONTINUE;
    res->incrementProgress = 100;
}

static void save_thread_func(UNUSED void *arg) {
    thread_ret = -1;

    pending_save_size = 0;

    sysSaveBufferSettings bufset;
    memset(&bufset, 0, sizeof(bufset));
    bufset.maxDirectories = SAVE_MAX_DIRS;
    bufset.maxFiles = SAVE_MAX_FILES;
    bufset.bufferSize = XMAX(
        SAVE_MAX_FILES * sizeof(sysSaveFileStatus),
        SAVE_MAX_DIRS * sizeof(sysSaveDirectoryList)
    );
    bufset.buffer = malloc(bufset.bufferSize);

    if (!bufset.buffer) {
        printf("save_thread: could not alloc %u bytes for save buffer\n", bufset.bufferSize);
        return;
    }

    sysMutexLock(save_mutex, 30000);

    if (thread_mode == MODE_SAVE) {
        thread_ret = sysSaveAutoSave2(
            SYS_SAVE_CURRENT_VERSION,
            SAVE_DIRNAME,
            SYS_SAVE_ERROR_DIALOG_SHOW_ONCE,
            &bufset,
            save_status_cb,
            save_file_cb,
            SYS_MEMORY_CONTAINER_INVALID,
            NULL
        );
    } else {
        thread_ret = sysSaveAutoLoad2(
            SYS_SAVE_CURRENT_VERSION,
            SAVE_DIRNAME,
            SYS_SAVE_ERROR_DIALOG_SHOW_ONCE,
            &bufset,
            save_status_cb,
            save_file_cb,
            SYS_MEMORY_CONTAINER_INVALID,
            NULL
        );
    }

    sysMutexUnlock(save_mutex);

    free(bufset.buffer);

    sysThreadExit(0);
}

static inline void wait_thread(void) {
    u64 retval;
    if (thread_mode != MODE_NONE) {
        sysThreadJoin(save_thread, &retval);
        thread_mode = MODE_NONE;
    }
}

static inline void spawn_thread(const int mode) {
    thread_mode = mode;
    sysThreadCreate(&save_thread, save_thread_func, NULL, 1500, 0x10000, THREAD_JOINABLE, "sm64 save");
}

bool ps3_save_init(void) {
    sys_mutex_attr_t mattr;
    sysMutexAttrInitialize(mattr);
    sysMutexCreate(&save_mutex, &mattr);
    init_done = true;
    return true;
}

bool ps3_save_write(const void *data, const uint32_t ofs, const uint32_t size) {
    sysMutexLock(save_mutex, 30000);
    memcpy(save_data + ofs, data, size);
    sysMutexUnlock(save_mutex);
    return true;
}

bool ps3_save_read(void *data, const uint32_t ofs, const uint32_t size) {
    sysMutexLock(save_mutex, 30000);
    memcpy(data, save_data + ofs, size);
    sysMutexUnlock(save_mutex);
    return true;
}

bool ps3_save_save(void) {
    wait_thread(); // wait for previous operation to complete
    spawn_thread(MODE_SAVE);
    // no use waiting for a pending save, we got a mutex on writes
    return true;
}

bool ps3_save_load(void) {
    wait_thread(); // wait for previous operation to complete
    spawn_thread(MODE_LOAD);
    wait_thread(); // wait until the save is done
    return (thread_ret == 0);
}

void ps3_save_shutdown(void) {
    if (init_done) {
        init_done = false;
        wait_thread();
        sysMutexDestroy(save_mutex);
    }
}

#endif // TARGET_PS3
