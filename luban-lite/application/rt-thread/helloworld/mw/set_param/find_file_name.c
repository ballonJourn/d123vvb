#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "find_file_name.h"
#include "mw_log.h"
#include "dev_config.h"
#ifndef _WIN32
#include <unistd.h>
#include <dirent.h>
#else
#include "lvgl/lvgl.h"
#endif

#define MAX_SUFFIX_FILE_NUM (2)
#define FILE_NAME_LEN       (64)

static char file_name[FILE_NAME_LEN];

char *find_file_name(const char *path, 
                    const char *suffix, 
                    const char *prefix) {
    if (!path || !suffix || !prefix) {
        mw_loge("param error!");
        return NULL;
    }
    
    char file_num = 0;
    char str_temp[MAX_SUFFIX_FILE_NUM][FILE_NAME_LEN] = {0};

#ifndef _WIN32
    DIR *dp;
    struct dirent  *dirp;

    dp = opendir(path);
    if (dp == NULL) {
        mw_loge("open %s floder failed\n", path);
        return NULL;
    }

    memset(file_name, 0, sizeof(file_name));
    while ((dirp = readdir(dp)) != NULL) {
        if ((strcmp(dirp->d_name, ".") == 0)
            || (strcmp(dirp->d_name, "..") == 0)) {
            continue;
        }

        if (dirp->d_type != DT_DIR) {
            if(strstr(dirp->d_name, suffix) == NULL) {
                continue;
            }

            char *str = strtok(dirp->d_name, ".");
            file_num++;
            memcpy(&str_temp[file_num - 1], str, strlen(str));
            if (file_num >= MAX_SUFFIX_FILE_NUM) {
                break;
            }
        }
    }
    closedir(dp);

    for (int i = 0; i < MAX_SUFFIX_FILE_NUM; i++) {
        if (strstr(str_temp[i], prefix)) {
            memcpy(file_name, str_temp[i], strlen(str_temp[i]));
            break;
        }
    }
#else
    lv_fs_dir_t d;
    char b[64] = {0};
    if (lv_fs_dir_open(&d, path) != LV_FS_RES_OK) {
        printf("open %s folder failed!\r\n", path);
        return NULL;
    }

    memset(file_name, 0, sizeof(file_name));
    while (lv_fs_dir_read(&d, b) == LV_FS_RES_OK) {
         if ((strcmp(b, ".") == 0)
            || (strcmp(b, "..") == 0)) {
            continue;
        }

        ///< 忽略windows路径下的文件夹
        if (strstr(b, "/") == NULL) {
            int len = strlen(b);
            if(strstr(b, suffix) == NULL) {
                continue;
            }

            char *str = strtok(b, ".");
            file_num++;
            memcpy(&str_temp[file_num - 1], str, strlen(str));
            if (file_num >= MAX_SUFFIX_FILE_NUM) {
                break;
            }
        }
    }

    lv_fs_dir_close(&d);

    for (int i = 0; i < MAX_SUFFIX_FILE_NUM; i++) {
        if (strstr(str_temp[i], prefix)) {
            memcpy(file_name, str_temp[i], strlen(str_temp[i]));
            break;
        }
    }

#endif
    return file_name;
}

uint8_t str_split(char *instr, char *token, 
                  char out_str[][SPLIT_STR_MAX_LEN],
                  int out_str_row) {
    char *result = NULL;
    result = strtok(instr, token);
    uint8_t i = 0;
    while (result != NULL && i < out_str_row) {
        snprintf(out_str[i], SPLIT_STR_MAX_LEN, "%s", result);
        result = strtok(NULL, token);
        i++;
    }

    return i;
}