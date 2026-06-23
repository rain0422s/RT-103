#ifndef __LFS_PORT_H__
#define __LFS_PORT_H__

#include "lfs.h"

/** 挂载一次，运行期间保持挂载；已挂载时直接返回 0 */
int lfs_mount_fs(void);

/** 关机/下电前调用，卸载文件系统 */
int lfs_unmount_fs(void);

/** 获取已挂载的 lfs 指针，供读写文件用；未挂载返回 NULL */
lfs_t *lfs_get(void);
int lfs_is_ready(void);

int lfs_migrate_from_old_offset(void);
int lfs_first_run(void);
uint32_t lfs_get_boot_count(void);

#endif
