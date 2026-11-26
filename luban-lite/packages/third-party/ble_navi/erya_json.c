/*
 * erya_json.c - JSON part of 尔雅 BLE 导航协议, on the SDK's cJSON-1.7.16
 * (packages/third-party/cJSON-1.7.16, LPKG_USING_CJSON).
 *
 * The document shows some numbers as JSON numbers ("num":1) and others as
 * strings ("type":"0"); both forms are accepted for every field.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "erya_proto.h"

static int get_u32(const cJSON *o, const char *key, uint32_t *v)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsNumber(it)) {
        if (it->valuedouble < 0) return -1;
        *v = (uint32_t)it->valuedouble;
        return 0;
    }
    if (cJSON_IsString(it) && it->valuestring) {
        char *end;
        unsigned long x = strtoul(it->valuestring, &end, 10);
        if (end == it->valuestring) return -1;
        *v = (uint32_t)x;
        return 0;
    }
    return -1;
}

/* value as text, whatever its JSON type; returns 0 if present */
static int get_text(const cJSON *o, const char *key, char *out, size_t cap)
{
    const cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    if (cJSON_IsString(it) && it->valuestring) {
        snprintf(out, cap, "%s", it->valuestring);
        return 0;
    }
    if (cJSON_IsNumber(it)) {
        snprintf(out, cap, "%ld", (long)it->valuedouble);
        return 0;
    }
    return -1;
}

static int fun_is(const cJSON *root, const char *name)
{
    const cJSON *f = cJSON_GetObjectItemCaseSensitive(root, "fun");
    return cJSON_IsString(f) && f->valuestring && !strcmp(f->valuestring, name);
}

int erya_json_pic(const char *json, uint32_t *num, uint32_t *sum, uint32_t *size)
{
    cJSON *root = cJSON_Parse(json);
    int rc = -1;
    if (root && fun_is(root, "pic") &&
        get_u32(root, "num", num) == 0 && get_u32(root, "sum", sum) == 0 &&
        get_u32(root, "size", size) == 0)
        rc = 0;
    cJSON_Delete(root);
    return rc;
}

static void fmt_distance(const char *m, char *out, size_t cap)
{
    char *end;
    unsigned long v = strtoul(m, &end, 10);
    if (end == m) { snprintf(out, cap, "%s", m); return; }
    if (v >= 1000)
        snprintf(out, cap, "%lu.%lukm", v / 1000, (v % 1000) / 100);
    else
        snprintf(out, cap, "%lum", v);
}

static void fmt_duration(const char *s, char *out, size_t cap)
{
    char *end;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s) { snprintf(out, cap, "%s", s); return; }
    if (v >= 3600)
        snprintf(out, cap, "%luh%02lumin", v / 3600, (v % 3600) / 60);
    else
        snprintf(out, cap, "%lumin", (v + 59) / 60);
}

int erya_json_navi(const char *json, struct nav_state *ns)
{
    cJSON *root = cJSON_Parse(json);
    if (!root || !fun_is(root, "navi")) {
        cJSON_Delete(root);
        return -1;
    }
    uint32_t type = 1;
    get_u32(root, "type", &type);          /* 0 start, 1 road info, 2 end */
    ns->is_navigation = 1;
    if (type == 2) {
        ns->active = 0;
        ns->title[0] = ns->directions[0] = ns->distance[0] = 0;
        ns->duration[0] = ns->eta[0] = 0;
        ns->turn = 0;
        cJSON_Delete(root);
        return 0;
    }
    ns->active = 1;
    const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "data");
    if (cJSON_IsObject(d)) {
        char t[CHRONOS_STR_MAX];
        uint32_t icon;
        if (get_text(d, "curStepRetainDistance", t, sizeof(t)) == 0)
            fmt_distance(t, ns->title, sizeof(ns->title));
        get_text(d, "nextRoadName", ns->directions, sizeof(ns->directions));
        if (get_text(d, "pathRetainTime", t, sizeof(t)) == 0)
            fmt_duration(t, ns->duration, sizeof(ns->duration));
        if (get_text(d, "pathRetainDistance", t, sizeof(t)) == 0)
            fmt_distance(t, ns->distance, sizeof(ns->distance));
        get_text(d, "arriveTime", ns->eta, sizeof(ns->eta));
        if (get_u32(d, "iconType", &icon) == 0)
            ns->turn = (uint8_t)icon;    /* 尔雅 icon id, mapping is app-defined */
    }
    cJSON_Delete(root);
    return 0;
}

int erya_json_fun(const char *json, char *out, size_t cap)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *f = root ? cJSON_GetObjectItemCaseSensitive(root, "fun") : NULL;
    int rc = -1;
    if (cJSON_IsString(f) && f->valuestring) {
        snprintf(out, cap, "%s", f->valuestring);
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

/* {"fun":"time","data":{"timeZone":"8","timestamp":"1795140207"}} */
int erya_json_time(const char *json, uint32_t *utc, int *tz_hours)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *d = root ? cJSON_GetObjectItemCaseSensitive(root, "data") : NULL;
    int rc = -1;
    uint32_t ts;
    char tz[16];
    if (cJSON_IsObject(d) && get_u32(d, "timestamp", &ts) == 0) {
        *utc = ts;
        *tz_hours = (get_text(d, "timeZone", tz, sizeof(tz)) == 0) ? atoi(tz) : 0;
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}

/* {"fun":"activate","type":"2"} */
int erya_json_activate(const char *json, int *type)
{
    cJSON *root = cJSON_Parse(json);
    uint32_t t;
    int rc = -1;
    if (root && get_u32(root, "type", &t) == 0) {
        *type = (int)t;
        rc = 0;
    }
    cJSON_Delete(root);
    return rc;
}
