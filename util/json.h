#ifndef JSON_UTIL_H
#define JSON_UTIL_H

#include <stdbool.h>

// build.json を読み込み、キーを書き換えて保存する。
bool json_write_string(const char *filename, const char *key, const char *value);
bool json_write_int(const char *filename, const char *key, int value);

// 空のオブジェクト {} をセット
bool json_write_object(const char *filename, const char *key);

// 空の配列 [] をセット
bool json_write_array(const char *filename, const char *key);

#endif
