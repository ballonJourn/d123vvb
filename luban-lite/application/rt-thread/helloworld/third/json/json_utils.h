/**
 * @file json_utils.h
 *
 * @brief This message displayed in Doxygen Files index
 *
 * @ingroup PackageName
 * (note: this needs exactly one @defgroup somewhere)
 *
 * @date	2024-11-15-12-08
 * @author Nemausa
 *
 */
#ifndef JSON_UTILS_H_
#define JSON_UTILS_H_

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

void json_utils_add_string(cJSON *json, const char *key, const char *value);
void json_utils_add_int(cJSON *json, const char *key, int value);
void json_utils_add_bool(cJSON *json, const char *key, int value);

void json_utils_delete_key(cJSON *json, const char *key);

void json_utils_modify_string(cJSON *json, const char *key,
                              const char *new_value);
void json_utils_modify_int(cJSON *json, const char *key, int new_value);
void json_utils_modify_bool(cJSON *json, const char *key, int new_value);

const char *json_utils_get_string(cJSON *json, const char *key);
int json_utils_get_int(cJSON *json, const char *key);
int json_utils_get_bool(cJSON *json, const char *key);

cJSON *json_utils_read_json(const char *filename);
int json_utils_save_json(const char *filename, cJSON *root);

#ifdef __cplusplus
}
#endif

#endif  // JSON_UTILS_H_
