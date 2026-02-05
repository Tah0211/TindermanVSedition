#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <stdbool.h>

// build.json を読み込み、キーを書き換えて保存する（トップレベルキー想定）
bool json_write_string(const char *filename, const char *key, const char *value);
bool json_write_int(const char *filename, const char *key, int value);

// 空のオブジェクト {} をセット
bool json_write_object(const char *filename, const char *key);

// 空の配列 [] をセット
bool json_write_array(const char *filename, const char *key);

// 追加：単一キーの読み込み（トップレベルのみ想定）
bool json_read_int(const char *filename, const char *key, int *out_value);
bool json_read_string(const char *filename, const char *key, char *out_buf, int out_buf_size);

#endif
