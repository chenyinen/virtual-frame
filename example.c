
#include <stdio.h>

#include "emu_coder.h"

int main()
{
    FILE *file;
    struct emu_encode_config encode_config;
    long task;
    unsigned char** frame_data;
    int frame_len;
    int i;
    encode_config.bit_rate = 100000;
    encode_config.coder = "libx265";
    encode_config.frame_rate = 25;
    encode_config.gop_size = 50;
    encode_config.width = 1024;
    encode_config.height = 768;

    task = emu_init(NULL, &encode_config);
    if (task < 0) {
        printf("emu init fail\n");
        return -1;
    }
    file = fopen("./test.h265", "w+");
    if (!file) {
        return -1;
    }
    for(i=0; i<100; i++){
        emu_get_frame_data((long)task, &frame_data, &frame_len);
        fwrite(*frame_data, frame_len, 1, file);
        printf("write frame len:%d\n", frame_len);
        emu_destroy_frame(frame_data);
    }

    return 0;
}
