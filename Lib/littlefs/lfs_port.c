#include "lfs.h"
#include "w25qxx.h"

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
	int ret = w25qxx_read((uint8_t *)buffer, c->block_size * block + off, size);
        if(!ret)
                return LFS_ERR_OK;
        else
                return ret;
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

        int ret = w25qxx_write((uint8_t *)buffer, c->block_size * block + off, size);
        if(!ret)
                return LFS_ERR_OK;
        else
                return ret;
}

/**
 * lfs与底层flash擦除接口
 * @param  c
 * @param  block 块编号
 * @return
 */
static int lfs_deskio_erase(const struct lfs_config *c, lfs_block_t block)
{
	int ret = w25qxx_erase_block(block);
        if(!ret)
                return LFS_ERR_OK;
        else
                return ret;
}

static int lfs_deskio_sync(const struct lfs_config *c)
{
        int ret = w25qxx_getstatus();
        if(!ret)
                return LFS_ERR_OK;
        else
                return ret;
}


///
/// 静态内存使用方式必须设定这四个缓存
///
// __align(4) static uint8_t read_buffer[16];
// __align(4) static uint8_t prog_buffer[16];
// __align(4) static uint8_t lookahead_buffer[16];



const struct lfs_config lfs_w25qxx_cfg =
{
	// block device operations
	.read  = lfs_deskio_read,
	.prog  = lfs_deskio_prog,
	.erase = lfs_deskio_erase,
	.sync  = lfs_deskio_sync,

	// block device configuration
	.read_size = 16,
	.prog_size = 16,
	.block_size = 4096,
	.block_count = 32,
	.cache_size = 4096,
	.lookahead_size = 4096,
	.block_cycles = 500,

	//
	// 使用静态内存必须设置这几个缓存
	//
	// .read_buffer = read_buffer,
	// .prog_buffer = prog_buffer,
	// .lookahead_buffer = lookahead_buffer,
};





lfs_t lfs;
lfs_file_t file;
lfs_dir_t  dir;
struct lfs_info info;

int lfs_test(void){

        int err = lfs_mount(&lfs, &lfs_w25qxx_cfg);//第一步要挂载文件系统
        if(err < 0){
                lfs_format(&lfs, &lfs_w25qxx_cfg);
                lfs_mount(&lfs, &lfs_w25qxx_cfg);
        }

	//以下操作都为假设操作成功
	//创建一个名为test的文件向文件中写入"1234"4个字节数据
	lfs_file_open(&lfs, &file, "test", LFS_O_CREAT | LFS_O_RDWR);
	lfs_file_write(&lfs, &file, "1234", 4);
	lfs_file_close(&lfs, &file);
	
        //这时虽然打开文件时也使用了LFS_O_CREAT标志但是并不会创建一个新的文件也不会报错，在加入LFS_O_EXCL标志后才会报错
        //LFS_O_RDONLY 标志表示以只读打开文件
        //LFS_O_WRONLY 标志表示以只写打开文件
        //LFS_O_RDWR 标志表示以可读可写打开文件，等价于 LFS_O_RDONLY | LFS_O_WRONLY
        //LFS_O_CREAT 打开文件时如果文件不存在就创建新文件并打开，如果存在将读写指针定位到文件开头打开文件
        //LFS_O_EXCL  打开文件时如果文件不存在就创建新文件并打开，如果存在就报错
        //LFS_O_TRUNC 打开一个已有文件并将文件大小设置为0
        //LFS_O_APPEND 打开一个已有文件并将文件的读写指针设置到文件最后

	lfs_file_open(&lfs, &file, "test", LFS_O_CREAT | LFS_O_RDWR);
	lfs_file_write(&lfs, &file, "abc", 3);
	lfs_file_sync(&lfs, &file);//这时会见内存中的缓存数据写入到Flash中，这时文件内容为"abc4"

        //LFS_SEEK_SET 用绝对位置设置文件的读写指针（用相对用文件开头的位置设置读写指针）
        //LFS_SEEK_CUR 用相对于当前的位置位置设置读写指针
        //LFS_SEEK_END 用相对用文件末尾的位置设置读写指针

	lfs_file_seek(&lfs, &file, 0, LFS_SEEK_SET);//文件指针返回到文件开头
	lfs_file_write(&lfs, &file, "1", 1);
	lfs_file_sync(&lfs, &file);//这时文件内容为"1bc4"

	lfs_file_seek(&lfs, &file, 0, LFS_SEEK_END);//文件指针设置到文件最后
	lfs_file_write(&lfs, &file, "5", 1);
	lfs_file_sync(&lfs, &file);//这时文件内容为"1bc45"

	//文件指针设置到相对于当前位置-2，1bc45| --> 1bc|45
	lfs_file_seek(&lfs, &file, -2, LFS_SEEK_CUR);
	lfs_file_write(&lfs, &file, "d", 1);
	lfs_file_sync(&lfs, &file);//这时文件内容为"1bcd5"
	lfs_file_close(&lfs, &file);

	//对test文件设置一个时间和一个日期的自定义属性，在删除文件时也会删除
	#define FILE_TIME_TYPE 1
	#define FILE_DATE_TYPE 2
	lfs_setattr(&lfs, "test", FILE_TIME_TYPE, "12:00:00", 8);
	lfs_setattr(&lfs, "test", FILE_DATE_TYPE, "2023-1-1", 8);

	//在根目录下创建了一个名为abc的目录
	//在abc目录下创建了一个名为test的文件，当前有两个test文件一个在根目录一个在abc目录中
	lfs_mkdir(&lfs, "abc");
	lfs_dir_open(&lfs, &dir, "abc");
	lfs_file_open(&lfs, &file, "test",LFS_O_CREAT | LFS_O_RDWR);
	lfs_file_close(&lfs, &file);
	lfs_dir_close(&lfs, &dir);

	//遍历根目录下的内容，会递归遍历根目录下的目录里的内容
	//同时每个目录都会遍历到一个"."和一个".."的文件夹表示当前文件夹和返回上一个文件夹的路径
	lfs_dir_open(&lfs, &dir, ".");
	while(1)
	{
		err = lfs_dir_read(&lfs, &dir, &info);
		if(err < 0)
			break;
	}
	lfs_dir_close(&lfs, &dir);

	lfs_unmount(&lfs);
        return 1;
}

