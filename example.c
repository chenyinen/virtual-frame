
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu_coder.h"
int main()
{
    FILE *file, *file2; 
    struct emu_encode_config encode_config;
    long task;
    unsigned char** frame_data;
    int frame_len;
    char buf[1024];
    int i,j,k=0;
    int *len_buf;
    encode_config.bit_rate = 100000;
    encode_config.coder = "libx264";
    encode_config.frame_rate = 25;
    encode_config.gop_size = 50;
    encode_config.width = 480;
    encode_config.height = 800;

    task = emu_init(NULL, &encode_config);
    if (task < 0) {
        printf("emu init fail\n");
        return -1;
    }
    file = fopen("./test.h264", "w+");
    file2 = fopen("./264.c", "w+");
    if (!file || !file2) {
        printf("open output file fail\n");
        return -1;
    }
    len_buf = malloc(sizeof(*len_buf)*encode_config.gop_size);
    for(i=0; i<encode_config.gop_size; i++){
        emu_get_frame_data((long)task, &frame_data, &frame_len);
        len_buf[i]=frame_len;
        printf("write frame len:%d\n", frame_len);
        fwrite(*frame_data, frame_len, 1, file);
        snprintf(buf, sizeof(buf)-1, "const unsigned char video_%s_%d_%d_%d[] = {", encode_config.coder, encode_config.width, encode_config.height, k++);
        fwrite(buf, strlen(buf), 1, file2);
        for(j=0; j< frame_len; j++) {
            snprintf(buf, sizeof(buf)-1, "0x%.2x,", (*frame_data)[j]);
            fwrite(buf, 5, 1, file2);
        }
        fwrite("};\n", 3, 1, file2);
        emu_destroy_frame(frame_data);
    }
    snprintf(buf, sizeof(buf)-1, "struct video_%s_t {\nunsigned char const *data;\nint data_len;\n};\n", encode_config.coder);
    fwrite(buf, strlen(buf), 1, file2);
    snprintf(buf, sizeof(buf)-1, "struct video_%s_t video_%s_%d_%d[] = {", encode_config.coder, encode_config.coder, encode_config.width, encode_config.height);
    fwrite(buf, strlen(buf), 1, file2);
    for(i=0; i<k; i++) {
        snprintf(buf, sizeof(buf)-1, "{video_%s_%d_%d_%d, %d},", encode_config.coder, encode_config.width, encode_config.height, i, len_buf[i]);
        fwrite(buf, strlen(buf), 1, file2);
    }
    fwrite("};\n", 3, 1, file2);
    fflush(file);
    fflush(file2);
    return 0;
}
