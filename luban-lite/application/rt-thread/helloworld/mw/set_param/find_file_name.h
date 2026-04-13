#ifndef __FIND_FILE_NAME_H__
#define __FIND_FILE_NAME_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>

#define SPLIT_STR_MAX_LEN   (64)

/**
 * @brief 获取特定文件路径下带前缀与后缀标识符的文件名
 * @param path 文件路径
 * @param suffix 文件后缀 如 .ini, .json
 * @param prefix 文件前缀 如 set_xxx_xxx.json
 * @return 成功:返回文件名  失败:NULL
 */
char *find_file_name(const char *path, 
                    const char *suffix, 
                    const char *prefix); 

/**
 * @brief 以特定符号分割字符串
 * @param instr 源字符串
 * @param token 分割标志字符串 例如“—” “.”等
 * @param out_str 存储分割字符串的数组
 * @param out_str_row 数组元素大小 
 * @return 分割后的字符串格个数
 */
uint8_t str_split(char *instr, char *token, 
                  char out_str[][SPLIT_STR_MAX_LEN], 
                  int out_str_row);

#ifdef __cplusplus
}
#endif //__cplusplus

#endif // __FIND_FILE_NAME_H__