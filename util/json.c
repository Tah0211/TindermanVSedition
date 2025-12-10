#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================
// 内部用：ファイルを丸ごと読み込む
// =============================================================
static char *read_file(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp)
        return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    char *buf = malloc(size + 1);
    fread(buf, 1, size, fp);
    buf[size] = '\0';
    fclose(fp);
    return buf;
}

// =============================================================
// 内部用：キーを探し、値を書き換える（超軽量）
// =============================================================
static char *json_set_key(const char *json, const char *key, const char *value)
{
    // 新しい JSON を構成し直す（安全第一）
    // JSON は常に top-level object のみを扱う。

    char *out = malloc(8192);
    strcpy(out, "{\n");

    const char *p = json;
    const char *kpos;

    // 1行ずつ走査
    while ((kpos = strstr(p, "\"")) != NULL)
    {
        const char *kend = strchr(kpos + 1, '"');
        if (!kend)
            break;

        size_t klen = kend - (kpos + 1);
        char cur_key[128];
        strncpy(cur_key, kpos + 1, klen);
        cur_key[klen] = '\0';

        const char *colon = strchr(kend, ':');
        if (!colon)
            break;

        const char *line_end = strchr(colon, ',');
        if (!line_end)
            line_end = strchr(colon, '\n');
        if (!line_end)
            line_end = colon;

        // キーが一致したら上書き
        char linebuf[256];
        if (strcmp(cur_key, key) == 0)
        {
            snprintf(linebuf, sizeof(linebuf),
                     "  \"%s\": %s,\n", key, value);
        }
        else
        {
            size_t len = line_end - kpos + 1;
            strncpy(linebuf, kpos, len);
            linebuf[len] = '\0';
        }

        strcat(out, linebuf);
        p = line_end + 1;
    }

    strcat(out, "}\n");
    return out;
}

// =============================================================
// JSON を書き戻す
// =============================================================
static bool write_json(const char *filename, const char *json)
{
    FILE *fp = fopen(filename, "w");
    if (!fp)
        return false;

    fputs(json, fp);
    fclose(fp);
    return true;
}

// =============================================================
// パブリック API
// =============================================================
bool json_write_string(const char *filename, const char *key, const char *value)
{
    char quoted[256];
    snprintf(quoted, sizeof(quoted), "\"%s\"", value);

    char *src = read_file(filename);
    if (!src)
    {
        // 新規ファイル
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\n  \"%s\": %s\n}\n", key, quoted);
        return write_json(filename, buf);
    }

    char *rewritten = json_set_key(src, key, quoted);
    bool ok = write_json(filename, rewritten);

    free(src);
    free(rewritten);
    return ok;
}

bool json_write_int(const char *filename, const char *key, int value)
{
    char val[64];
    snprintf(val, sizeof(val), "%d", value);

    char *src = read_file(filename);
    if (!src)
    {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\n  \"%s\": %s\n}\n", key, val);
        return write_json(filename, buf);
    }

    char *rewritten = json_set_key(src, key, val);
    bool ok = write_json(filename, rewritten);

    free(src);
    free(rewritten);
    return ok;
}

bool json_write_object(const char *filename, const char *key)
{
    return json_write_string(filename, key, "{}");
}

bool json_write_array(const char *filename, const char *key)
{
    return json_write_string(filename, key, "[]");
}
