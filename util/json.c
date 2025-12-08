#include "json.h"
#include <stdio.h>

void json_write_string(const char *file, const char *key, const char *value)
{
    FILE *fp = fopen(file, "w");
    if (!fp)
        return;

    fprintf(fp, "{ \"%s\": \"%s\" }\n", key, value);

    fclose(fp);
}
