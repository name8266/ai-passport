#pragma once
typedef int wl_handle_t;
#define WL_INVALID_HANDLE (-1)
static int test_wl_unmount_error,test_wl_unmounts;
static inline int wl_unmount(wl_handle_t handle) {
    (void)handle;test_wl_unmounts++;return test_wl_unmount_error;
}
