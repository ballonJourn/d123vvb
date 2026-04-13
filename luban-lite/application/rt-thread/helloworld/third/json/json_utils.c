#include "json_utils.h"
#include <stdio.h>
#include <stdlib.h>

void json_utils_add_string(cJSON *json, const char *key, const char *value) {
  cJSON_AddStringToObject(json, key, value);
}

void json_utils_add_int(cJSON *json, const char *key, int value) {
  cJSON_AddNumberToObject(json, key, value);
}

void json_utils_add_bool(cJSON *json, const char *key, int value) {
  cJSON_AddBoolToObject(json, key, value);
}

void json_utils_delete_key(cJSON *json, const char *key) {
  cJSON_DeleteItemFromObject(json, key);
}

void json_utils_modify_string(cJSON *json, const char *key,
                              const char *new_value) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsString(item)) {
    cJSON_SetValuestring(item, new_value);
  }
}

void json_utils_modify_int(cJSON *json, const char *key, int new_value) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsNumber(item)) {
    cJSON_SetIntValue(item, new_value);
  } else if (!item) {
    cJSON_AddNumberToObject(json, key, new_value);
  }
}

void json_utils_modify_bool(cJSON *json, const char *key, int new_value) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsBool(item)) {
    item->type = new_value ? cJSON_True : cJSON_False;
  }
}

const char *json_utils_get_string(cJSON *json, const char *key) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsString(item)) {
    return item->valuestring;
  }
  return NULL;
}

int json_utils_get_int(cJSON *json, const char *key) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsNumber(item)) {
    return item->valueint;
  }
  return 0;
}

int json_utils_get_bool(cJSON *json, const char *key) {
  cJSON *item = cJSON_GetObjectItem(json, key);
  if (item && cJSON_IsBool(item)) {
    return item->type == cJSON_True;
  }
  return 0;
}

char *json_utils_read_file(const char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file) {
    perror("Failed to open file");
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  fseek(file, 0, SEEK_SET);

  char *content = (char *)malloc(length + 1);
  if (!content) {
    perror("Failed to allocate memory");
    fclose(file);
    return NULL;
  }

  fread(content, 1, length, file);
  content[length] = '\0';

  fclose(file);
  return content;
}

int json_utils_write_file(const char *filename, const char *data) {
  FILE *file = fopen(filename, "w");
  if (!file) {
    perror("Failed to open file for writing");
    return -1;
  }

  fprintf(file, "%s", data);
  fclose(file);
  return 0;
}

cJSON *json_utils_read_json(const char *filename) {
  char *json_content = json_utils_read_file(filename);
  if (!json_content) {
    return NULL;
  }

  cJSON *root = cJSON_Parse(json_content);
  free(json_content);

  if (!root) {
    printf("Error parsing JSON: %s\n", cJSON_GetErrorPtr());
    return NULL;
  }

  return root;
}

int json_utils_save_json(const char *filename, cJSON *root) {
  if (!root) {
    printf("No JSON object to save.\n");
    return -1;
  }

  char *json_string = cJSON_Print(root);
  if (!json_string) {
    printf("Failed to stringify JSON.\n");
    return -1;
  }

  int result = json_utils_write_file(filename, json_string);
  free(json_string);
  return result;
}
