#ifndef EMU_CODER__H_
#define EMU_CODER__H_

#include <semaphore.h>

#define CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/* 编码配置 */
struct emu_encode_config {
    int width;
    int height;
    int frame_rate;
    int gop_size;
    const char* coder;
    int64_t bit_rate;
};
/**
 * @brief 初始化编解码设置, 当file存在时码流默认以file文件为模板,如果不存在则使用
 *        默认的编码源编码
 */
long emu_init(char *file, struct emu_encode_config *encode_config);
/**
 * @brief 销毁编解码任务
 */
void emu_destroy_task(long emu_handle);
/**
 * @brief 释放帧数据
 */
void emu_destroy_frame(unsigned char **data);
/**
 * @brief 获取一帧码流数据
 */
int emu_get_frame_data(long emu_handle, unsigned char***data, int *len);


#endif