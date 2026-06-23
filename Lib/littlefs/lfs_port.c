#include "lfs.h"
#include "w25qxx.h"
#include "lfs_port.h"
#include "ota_layout.h"
#include "utils.h"
#include "FreeRTOS.h"
#include "semphr.h"
#define LFS_PORT_FILE_MAX      4096U

static uint32_t s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;

static uint32_t lfs_block_addr(const struct lfs_config *c, lfs_block_t block, lfs_off_t off)
{
        return (uint32_t)((s_lfs_offset_blocks + (uint32_t)block) * c->block_size + off);
}
/**
 * lfs与底层flash读数据接口
 * @param  c
 * @param  block  块编号
 * @param  off    块内偏移地址
 * @param  buffer 用于存储读取到的数据
 * @param  size   要读取的字节数
 * @return
 */
static int lfs_deskio_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size)
{
	if(W25Qx_OK == w25qxx_read((uint8_t *)buffer, lfs_block_addr(c, block, off), size))
                return LFS_ERR_OK;
        else
                return LFS_ERR_IO;
}

/**
 * lfs与底层flash写数据接口
 * @param  c
 * @param  block  块编号
 * @param  off    块内偏移地址
 * @param  buffer 待写入的数据
 * @param  size   待写入数据的大小
 * @return
 */
static int lfs_deskio_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size)
{

        if(W25Qx_OK == w25qxx_write((uint8_t *)buffer, lfs_block_addr(c, block, off), size))
                return LFS_ERR_OK;
        else
                return LFS_ERR_IO;
}

/**
 * lfs与底层flash擦除接口
 * @param  c
 * @param  block 块编号
 * @return
 */
static int lfs_deskio_erase(const struct lfs_config *c, lfs_block_t block)
{
        DBG_PRINTF("block:%lu,i:%lu",
                   (unsigned long)block,
                   (unsigned long)lfs_block_addr(c, block, 0));
	if(W25Qx_OK == w25qxx_erase_block(lfs_block_addr(c, block, 0)))
                return LFS_ERR_OK;
        else
                return LFS_ERR_IO;
}

static int lfs_deskio_sync(const struct lfs_config *c)
{
        if(W25Qx_OK == w25qxx_getstatus())
                return LFS_ERR_OK;
        else
                return LFS_ERR_IO;
}

static SemaphoreHandle_t s_lfs_lock;

static int lfs_port_lock(const struct lfs_config *c)
{
        (void)c;
        if (s_lfs_lock == NULL) {
                s_lfs_lock = xSemaphoreCreateBinary();
                if (s_lfs_lock == NULL)
                        return LFS_ERR_NOMEM;
                (void)xSemaphoreGive(s_lfs_lock);
        }
        if (xSemaphoreTake(s_lfs_lock, portMAX_DELAY) != pdTRUE)
                return LFS_ERR_IO;
        return LFS_ERR_OK;
}

static int lfs_port_unlock(const struct lfs_config *c)
{
        (void)c;
        if (s_lfs_lock == NULL)
                return LFS_ERR_IO;
        (void)xSemaphoreGive(s_lfs_lock);
        return LFS_ERR_OK;
}





///
/// 静态内存使用方式必须设定这四个缓存
///
__attribute__((aligned(4))) static uint8_t read_buffer[256];
__attribute__((aligned(4))) static uint8_t prog_buffer[256];
__attribute__((aligned(4))) static uint8_t lookahead_buffer[256];


const struct lfs_config lfs_w25qxx_cfg =
{
        // block device operations
        .read  = lfs_deskio_read,
        .prog  = lfs_deskio_prog,
        .erase = lfs_deskio_erase,
        .sync  = lfs_deskio_sync,
        .lock  = lfs_port_lock,
        .unlock = lfs_port_unlock,

        // block device configuration
        .read_size = 256,
        .prog_size = 256,
        .block_size = 4096,
        .block_count = OTA_LFS_BLOCK_COUNT,
        .cache_size = 256,
        .lookahead_size = 128,
        .block_cycles = 500,

        .name_max=128,
        .file_max=LFS_PORT_FILE_MAX,
        .attr_max=128,
        // .context=512,

        // 使用静态内存必须设置这几个缓存

        .read_buffer = read_buffer,
        .prog_buffer = prog_buffer,
        .lookahead_buffer = lookahead_buffer,
};

static const struct lfs_config lfs_w25qxx_old_cfg =
{
        // block device operations
        .read  = lfs_deskio_read,
        .prog  = lfs_deskio_prog,
        .erase = lfs_deskio_erase,
        .sync  = lfs_deskio_sync,
        .lock  = lfs_port_lock,
        .unlock = lfs_port_unlock,

        // block device configuration
        .read_size = 256,
        .prog_size = 256,
        .block_size = 4096,
        .block_count = OTA_OLD_LFS_BLOCK_COUNT,
        .cache_size = 256,
        .lookahead_size = 128,
        .block_cycles = 500,

        .name_max=128,
        .file_max=LFS_PORT_FILE_MAX,
        .attr_max=128,
        // .context=512,

        // 浣跨敤闈欐€佸唴瀛樺繀椤昏缃繖鍑犱釜缂撳瓨

        .read_buffer = read_buffer,
        .prog_buffer = prog_buffer,
        .lookahead_buffer = lookahead_buffer,
};

int bytes_to_mb(uint32_t bytes) {
        // 1MB = 2^20 = 1048576 bytes
        // 先把 bytes 放大 1000 倍（为了获得小数点后三位）
        // 避免乘法溢出，uint64_t 容量要够大
        uint64_t bytes_x1000 = (uint64_t)bytes * 1000;

        // 整数除法，计算放大1000倍的 MB 数
        uint32_t mb_x1000 = (uint32_t)(bytes_x1000 >> 20); // 除以 2^20

        // 分离整数和小数部分
        uint32_t mb = mb_x1000 / 1000;
        uint32_t mb_frac = mb_x1000 % 1000;

        DBG_PRINTF("Converted to: %lu.%03lu MB\n",
                   (unsigned long)mb,
                   (unsigned long)mb_frac);
        return 0;
}

/* 全局挂载：一直挂载，关机时再卸载 */
static lfs_t s_lfs;
static int s_mounted = 0;
static int s_ready = 0;
static uint32_t s_boot_count = 0;
static uint8_t s_lfs_migrate_buffer[LFS_PORT_FILE_MAX];
static int s_lfs_migration_checked;

static const char *const s_lfs_migrate_files[] = {
        "boot_count",
        "battery_hist",
        "uptime_ckpt",
};

static int lfs_mount_raw_at(uint32_t offset_blocks, const struct lfs_config *cfg)
{
        s_lfs_offset_blocks = offset_blocks;
        return lfs_mount(&s_lfs, cfg);
}

static void lfs_force_unmount_raw(void)
{
        (void)lfs_unmount(&s_lfs);
        s_mounted = 0;
        s_ready = 0;
}

static lfs_ssize_t lfs_read_migrate_file(const char *path)
{
        lfs_file_t file;
        lfs_soff_t file_size;
        lfs_ssize_t read_len;
        int err;

        err = lfs_mount_raw_at(OTA_OLD_LFS_OFFSET_BLOCKS, &lfs_w25qxx_old_cfg);
        if (err < 0) {
                lfs_force_unmount_raw();
                return err;
        }

        err = lfs_file_open(&s_lfs, &file, path, LFS_O_RDONLY);
        if (err < 0) {
                lfs_force_unmount_raw();
                return err;
        }

        file_size = lfs_file_size(&s_lfs, &file);
        if (file_size < 0 || file_size > (lfs_soff_t)sizeof(s_lfs_migrate_buffer)) {
                (void)lfs_file_close(&s_lfs, &file);
                lfs_force_unmount_raw();
                return LFS_ERR_NOMEM;
        }

        read_len = lfs_file_read(&s_lfs, &file, s_lfs_migrate_buffer, (lfs_size_t)file_size);
        (void)lfs_file_close(&s_lfs, &file);
        lfs_force_unmount_raw();
        return read_len;
}

static int lfs_write_migrate_file(const char *path, lfs_size_t size)
{
        lfs_file_t file;
        lfs_ssize_t written = 0;
        int err;

        err = lfs_mount_raw_at(OTA_LFS_OFFSET_BLOCKS, &lfs_w25qxx_cfg);
        if (err < 0) {
                lfs_force_unmount_raw();
                return err;
        }

        err = lfs_file_open(&s_lfs, &file, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (err < 0) {
                lfs_force_unmount_raw();
                return err;
        }

        if (size > 0)
                written = lfs_file_write(&s_lfs, &file, s_lfs_migrate_buffer, size);
        err = lfs_file_close(&s_lfs, &file);
        lfs_force_unmount_raw();
        if (err < 0)
                return err;
        return (written == (lfs_ssize_t)size) ? 0 : LFS_ERR_IO;
}

int lfs_migrate_from_old_offset(void)
{
        int err;
        int formatted_new = 0;

        if (s_lfs_migration_checked)
                return 0;
        s_lfs_migration_checked = 1;

        if (lfs_mount_raw_at(OTA_LFS_OFFSET_BLOCKS, &lfs_w25qxx_cfg) == 0) {
                lfs_force_unmount_raw();
                return 0;
        }
        lfs_force_unmount_raw();

        err = lfs_mount_raw_at(OTA_OLD_LFS_OFFSET_BLOCKS, &lfs_w25qxx_old_cfg);
        lfs_force_unmount_raw();
        if (err < 0) {
                s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
                return 0;
        }

        for (unsigned int i = 0; i < sizeof(s_lfs_migrate_files) / sizeof(s_lfs_migrate_files[0]); i++) {
                lfs_ssize_t read_len = lfs_read_migrate_file(s_lfs_migrate_files[i]);
                if (read_len < 0)
                        continue;

                if (!formatted_new) {
                        s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
                        err = lfs_format(&s_lfs, &lfs_w25qxx_cfg);
                        if (err < 0)
                                break;
                        formatted_new = 1;
                }

                (void)lfs_write_migrate_file(s_lfs_migrate_files[i], (lfs_size_t)read_len);
        }

        lfs_force_unmount_raw();
        s_lfs_offset_blocks = OTA_LFS_OFFSET_BLOCKS;
        return 0;
}

static int lfs_write_boot_count_value(uint32_t boot_count)
{
        lfs_file_t file;
        int err = lfs_file_open(&s_lfs, &file, "boot_count",
                                LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);

        if (err < 0)
                return err;
        err = lfs_file_write(&s_lfs, &file, &boot_count, sizeof(boot_count));
        if (lfs_file_close(&s_lfs, &file) < 0)
                return LFS_ERR_IO;
        return (err == (int)sizeof(boot_count)) ? 0 : LFS_ERR_IO;
}

static int lfs_read_boot_count_value(uint32_t *boot_count)
{
        lfs_file_t file;
        int err;
        lfs_ssize_t read_len;

        if (boot_count == NULL)
                return LFS_ERR_INVAL;
        err = lfs_file_open(&s_lfs, &file, "boot_count", LFS_O_RDONLY);
        if (err < 0)
                return err;
        read_len = lfs_file_read(&s_lfs, &file, boot_count, sizeof(*boot_count));
        (void)lfs_file_close(&s_lfs, &file);
        return (read_len == (lfs_ssize_t)sizeof(*boot_count)) ? 0 : LFS_ERR_IO;
}

static int lfs_ensure_file_limit(void)
{
        struct lfs_fsinfo fsinfo;
        uint32_t saved_boot_count = 0;
        int has_boot_count;
        int err;

        if (!s_mounted)
                return LFS_ERR_IO;
        err = lfs_fs_stat(&s_lfs, &fsinfo);
        if (err < 0)
                return err;
        DBG_PRINTF("[lfs] limits name=%lu file=%lu attr=%lu\n",
                   (unsigned long)fsinfo.name_max,
                   (unsigned long)fsinfo.file_max,
                   (unsigned long)fsinfo.attr_max);
        if (fsinfo.file_max >= LFS_PORT_FILE_MAX)
                return 0;

        has_boot_count = (lfs_read_boot_count_value(&saved_boot_count) == 0);
        DBG_PRINTF("[lfs] migrate file_max %lu -> %lu\n",
                   (unsigned long)fsinfo.file_max,
                   (unsigned long)LFS_PORT_FILE_MAX);
        err = lfs_unmount_fs();
        if (err < 0)
                return err;
        err = lfs_format(&s_lfs, &lfs_w25qxx_cfg);
        if (err < 0)
                return err;
        err = lfs_mount_fs();
        if (err < 0)
                return err;
        if (has_boot_count)
                (void)lfs_write_boot_count_value(saved_boot_count);
        return 0;
}

int lfs_mount_fs(void)
{
        if (s_mounted)
                return 0;
        int err = lfs_mount(&s_lfs, &lfs_w25qxx_cfg);
        if (err) {
                DBG_PRINTF("lfs_mount failed: %d\n", err);
                if (err == LFS_ERR_CORRUPT) {
                        DBG_PRINTF("lfs_format after corrupt mount\n");
                        err = lfs_format(&s_lfs, &lfs_w25qxx_cfg);
                        DBG_PRINTF("lfs_format: %d\n", err);
                        if (err == 0)
                                err = lfs_mount(&s_lfs, &lfs_w25qxx_cfg);
                }
        }
        if (err == 0) {
                s_mounted = 1;
                s_ready = 0;
        }
        return err;
}

int lfs_unmount_fs(void)
{
        if (!s_mounted)
                return 0;
        int err = lfs_unmount(&s_lfs);
        if (err == 0) {
                s_mounted = 0;
                s_ready = 0;
        }
        return err;
}

lfs_t *lfs_get(void)
{
        return s_mounted ? &s_lfs : NULL;
}

int lfs_is_ready(void)
{
        return s_mounted && s_ready;
}

uint32_t lfs_get_boot_count(void)
{
        return s_boot_count;
}

int lfs_first_run(void)
{
        lfs_file_t file;
        (void)lfs_migrate_from_old_offset();
        int err = lfs_mount_fs();
        if (err)
                return err;
        err = lfs_ensure_file_limit();
        if (err)
                return err;
        if (s_ready)
                return 0;

        /* 检查剩余空间 */
        int total_blocks = lfs_w25qxx_cfg.block_count;
        int used_blocks = lfs_fs_size(&s_lfs);
        int free_blocks = total_blocks - used_blocks;
        DBG_PRINTF("Free space: %d blocks (%lu bytes)\n",
                   free_blocks,
                   (unsigned long)((lfs_size_t)free_blocks * lfs_w25qxx_cfg.block_size));
        bytes_to_mb(free_blocks * lfs_w25qxx_cfg.block_size);

        /* boot_count */
        uint32_t boot_count = 0;
        lfs_file_open(&s_lfs, &file, "boot_count", LFS_O_RDWR | LFS_O_CREAT);
        lfs_file_read(&s_lfs, &file, &boot_count, sizeof(boot_count));
        boot_count += 1;
        lfs_file_rewind(&s_lfs, &file);
        lfs_file_write(&s_lfs, &file, &boot_count, sizeof(boot_count));
        lfs_file_close(&s_lfs, &file);
        s_boot_count = boot_count;
        DBG_PRINTF("boot_count: %d\n", (int)boot_count);

        /* 列目录 */
        lfs_dir_t dir;
        struct lfs_info info;
        lfs_dir_open(&s_lfs, &dir, "/");
        while (lfs_dir_read(&s_lfs, &dir, &info)) {
                DBG_PRINTF("%s (%s, size: %lu)\n",
                           info.name,
                           info.type == LFS_TYPE_REG ? "file" : "dir",
                           (unsigned long)info.size);
        }
        lfs_dir_close(&s_lfs, &dir);
        s_ready = 1;

        /* 不卸载，保持挂载 */
        return 0;
}
