#include "lfs.h"
#include "w25qxx.h"
#include "lfs_port.h"
#define OFFSETBLOCK 		3
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
	if(W25Qx_OK == w25qxx_read((uint8_t *)buffer, c->block_size * (OFFSETBLOCK+block) + off, size))
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

        if(W25Qx_OK == w25qxx_write((uint8_t *)buffer, c->block_size * (OFFSETBLOCK+block) + off, size))
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
        printf("block:%d,i:%d",block,(OFFSETBLOCK+block)*c->block_size);
	if(W25Qx_OK == w25qxx_erase_block((OFFSETBLOCK+block)*c->block_size))
	// int ret = w25qxx_erase_block((OFFSETBLOCK+block));
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

        // block device configuration
        .read_size = 256,
        .prog_size = 256,
        .block_size = 4096,
        .block_count = 512,
        .cache_size = 256,
        .lookahead_size = 128,
        .block_cycles = 500,

        .name_max=128,
        .file_max=128, 
        .attr_max=128,
        // .context=512,

        // 使用静态内存必须设置这几个缓存

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

        printf("Converted to: %u.%03u MB\n", mb, mb_frac);
        return 0;
}


int lfs_first_run(void){
        lfs_t lfs;
        lfs_file_t file;
        // w25qxx_erase_chip();
        // mount the filesystem
        int err = lfs_mount(&lfs, &lfs_w25qxx_cfg);

        // reformat if we can't mount the filesystem
        // this should only happen on the first boot
        if (err) {
                lfs_format(&lfs, &lfs_w25qxx_cfg);
                lfs_mount(&lfs, &lfs_w25qxx_cfg);
        }

        // 检查剩余空间
        int total_blocks = lfs_w25qxx_cfg.block_count;
        int used_blocks = lfs_fs_size(&lfs);
        int free_blocks = total_blocks - used_blocks;
        printf("Free space: %d blocks (%d bytes)\n", 
        free_blocks, free_blocks * lfs_w25qxx_cfg.block_size);
        bytes_to_mb(free_blocks * lfs_w25qxx_cfg.block_size);


        // // 写入文件
        // lfs_file_t file;
        // lfs_file_open(&lfs, &file, "test.txt", LFS_O_WRONLY | LFS_O_CREAT);
        // lfs_file_write(&lfs, &file, "Hello World", 11);
        // lfs_file_close(&lfs, &file);

        // // 读取文件
        // lfs_file_open(&lfs, &file, "test.txt", LFS_O_RDONLY);
        // char buffer[64];
        // lfs_file_read(&lfs, &file, buffer, sizeof(buffer));
        // lfs_file_close(&lfs, &file);
        // printf("test.txt: %s\n", buffer);

        // // 删除文件
        // err = lfs_remove(&lfs, "test.txt");
        // if (err) {
        //         printf("remove test.txt fail\n");
        // }

        // read current count
        uint32_t boot_count = 0;
        lfs_file_open(&lfs, &file, "boot_count", LFS_O_RDWR | LFS_O_CREAT);
        lfs_file_read(&lfs, &file, &boot_count, sizeof(boot_count));

        // update boot count
        boot_count += 1;
        lfs_file_rewind(&lfs, &file);
        lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));

        // remember the storage is not updated until the file is closed successfully
        lfs_file_close(&lfs, &file);

        // print the boot count
        printf("boot_count: %d\n", boot_count);


        lfs_dir_t dir;
        struct lfs_info info;

        lfs_dir_open(&lfs, &dir, "/");
        while (lfs_dir_read(&lfs, &dir, &info)) {
        printf("%s (%s, size: %d)\n", 
                info.name,
                info.type == LFS_TYPE_REG ? "file" : "dir",
                info.size);
        }
        lfs_dir_close(&lfs, &dir);

        // release any resources we were using
        lfs_unmount(&lfs);
}



