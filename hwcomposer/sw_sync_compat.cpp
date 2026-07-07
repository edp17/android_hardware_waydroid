/*
 * Compatibility implementations for legacy libsync sw_sync helpers.
 *
 * Android 11 vendor builds may link hwcomposer.waydroid against the
 * libsync LLNDK stub, which does not export sw_sync_timeline_create(),
 * sw_sync_timeline_inc(), or sw_sync_fence_create().
 *
 * The Waydroid HWC still uses these helpers, so provide the small ioctl
 * wrappers locally.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if __has_include(<linux/sw_sync.h>)
#include <linux/sw_sync.h>
#endif

#ifndef SW_SYNC_IOC_MAGIC
#include <linux/types.h>

struct sw_sync_create_fence_data {
    __u32 value;
    char name[32];
    __s32 fence;
};

#define SW_SYNC_IOC_MAGIC 'W'
#define SW_SYNC_IOC_CREATE_FENCE _IOWR(SW_SYNC_IOC_MAGIC, 0, struct sw_sync_create_fence_data)
#define SW_SYNC_IOC_INC _IOW(SW_SYNC_IOC_MAGIC, 1, __u32)
#endif

extern "C" int sw_sync_timeline_create(void) {
    return open("/dev/sw_sync", O_RDWR | O_CLOEXEC);
}

extern "C" int sw_sync_timeline_inc(int fd, unsigned int count) {
    __u32 value = count;
    return ioctl(fd, SW_SYNC_IOC_INC, &value);
}

extern "C" int sw_sync_fence_create(int fd, const char* name, unsigned int value) {
    struct sw_sync_create_fence_data data;
    memset(&data, 0, sizeof(data));

    data.value = value;
    data.fence = -1;

    const char* fence_name = name ? name : "waydroid";
    strncpy(data.name, fence_name, sizeof(data.name) - 1);
    data.name[sizeof(data.name) - 1] = '\0';

    if (ioctl(fd, SW_SYNC_IOC_CREATE_FENCE, &data) < 0)
        return -errno;

    return data.fence;
}
