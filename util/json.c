#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// =============================================================
// 内部：ファイルを丸ごと読み込む
// =============================================================
static char *read_file(const char *filename)
{
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    rewind(fp);

    if (size < 0)
    {
        fclose(fp);
        return NULL;
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf)
    {
        fclose(fp);
        return NULL;
    }

    size_t n = fread(buf, 1, size, fp);
    buf[n] = '\0';
    fclose(fp);
    return buf;
}

// =============================================================
// 内部：JSONを書き戻す
// =============================================================
static bool write_json(const char *filename, const char *json)
{
    FILE *fp = fopen(filename, "w");
    if (!fp) return false;
    fputs(json, fp);
    fclose(fp);
    return true;
}

// =============================================================
// 内部：末尾の , を削除（JSON破壊防止）
// =============================================================
static void strip_trailing_comma(char *s)
{
    char *p = s + strlen(s) - 1;
    while (p > s && isspace((unsigned char)*p)) p--;
    if (*p == ',') *p = '\0';
}

// =============================================================
// 内部：キーを探して値を書き換え or 追加（安全版）
// =============================================================
static char *json_set_key(const char *json, const char *key, const char *value)
{
    size_t out_cap = strlen(json) + strlen(key) + strlen(value) + 256;
    char *out = (char *)malloc(out_cap);
    if (!out) return NULL;

    strcpy(out, "{\n");

    const char *p = json;
    bool written = false;

    while ((p = strchr(p, '"')) != NULL)
    {
        const char *kend = strchr(p + 1, '"');
        if (!kend) break;

        size_t klen = (size_t)(kend - (p + 1));
        char cur_key[128];
        if (klen == 0 || klen >= sizeof(cur_key)) break;
        strncpy(cur_key, p + 1, klen);
        cur_key[klen] = '\0';

        const char *colon = strchr(kend, ':');
        if (!colon) break;

        const char *line_end = strchr(colon, '\n');
        if (!line_end) line_end = colon + strlen(colon);

        // 値部分を抜き出してトリム（末尾カンマも除去）
        const char *vs = colon + 1;
        while (vs < line_end && isspace((unsigned char)*vs)) vs++;

        const char *ve = line_end;
        while (ve > vs && isspace((unsigned char)ve[-1])) ve--;
        if (ve > vs && ve[-1] == ',') ve--;                 // 末尾カンマ除去
        while (ve > vs && isspace((unsigned char)ve[-1])) ve--;

        char valbuf[512];
        size_t vlen = (size_t)(ve - vs);
        if (vlen >= sizeof(valbuf)) vlen = sizeof(valbuf) - 1;
        memcpy(valbuf, vs, vlen);
        valbuf[vlen] = '\0';

        char linebuf[768];

        if (strcmp(cur_key, key) == 0)
        {
            // 指定キーは上書き（valueは呼び出し側で "..." など整形済み）
            snprintf(linebuf, sizeof(linebuf), "  \"%s\": %s,\n", key, value);
            written = true;
        }
        else
        {
            // 既存キーも必ず「正規化して」出力（←ここが重要）
            snprintf(linebuf, sizeof(linebuf), "  \"%s\": %s,\n", cur_key, valbuf);
        }

        strcat(out, linebuf);
        p = line_end + 1;
    }

    if (!written)
    {
        snprintf(out + strlen(out), out_cap - strlen(out),
                 "  \"%s\": %s,\n", key, value);
    }

    strip_trailing_comma(out);
    strcat(out, "\n}\n");
    return out;
}


// =============================================================
// write API
// =============================================================
bool json_write_string(const char *filename, const char *key, const char *value)
{
    char quoted[256];
    snprintf(quoted, sizeof(quoted), "\"%s\"", value);

    char *src = read_file(filename);
    if (!src)
    {
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "{\n  \"%s\": %s\n}\n", key, quoted);
        return write_json(filename, buf);
    }

    char *rewritten = json_set_key(src, key, quoted);
    if (!rewritten)
    {
        free(src);
        return false;
    }

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
    if (!rewritten)
    {
        free(src);
        return false;
    }

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

// =============================================================
// read API（トップレベル簡易読み）
// =============================================================
bool json_read_int(const char *filename, const char *key, int *out_value)
{
    if (!filename || !key || !out_value) return false;

    FILE *fp = fopen(filename, "r");
    if (!fp) return false;

    char line[512];
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    while (fgets(line, sizeof(line), fp))
    {
        char *p = strstr(line, pat);
        if (!p) continue;

        p = strchr(p, ':');
        if (!p) continue;
        p++;

        while (*p && isspace((unsigned char)*p)) p++;
        *out_value = atoi(p);

        fclose(fp);
        return true;
    }

    fclose(fp);
    return false;
}

bool json_read_string(const char *filename, const char *key,
                      char *out_buf, int out_buf_size)
{
    if (!filename || !key || !out_buf || out_buf_size <= 0) return false;

    FILE *fp = fopen(filename, "r");
    if (!fp) return false;

    char line[512];
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    while (fgets(line, sizeof(line), fp))
    {
        char *p = strstr(line, pat);
        if (!p) continue;

        p = strchr(p, ':');
        if (!p) continue;
        p++;

        while (*p && isspace((unsigned char)*p)) p++;
        if (*p != '"') continue;
        p++;

        int i = 0;
        while (*p && *p != '"' && i < out_buf_size - 1)
        {
            if (*p == '\\' && p[1])
            {
                p++;
                if (*p == 'n') out_buf[i++] = '\n';
                else if (*p == 't') out_buf[i++] = '\t';
                else out_buf[i++] = *p;
                p++;
            }
            else
            {
                out_buf[i++] = *p++;
            }
        }
        out_buf[i] = '\0';

        fclose(fp);
        return true;
    }

    fclose(fp);
    return false;
}
