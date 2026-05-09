#include "tools/tools.h"
#include "tools/sandbox.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE_SIZE (10 * 1024 * 1024)  /* 10 MB */

static ToolResult read_exec(cJSON *args);

ToolDef read_file_def = {
    .name = "read_file",
    .desc = "Read the contents of a file in the workspace.",
    .param_schema = "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"File path relative to workspace\"}},\"required\":[\"path\"]}",
    .exec = read_exec,
    .read_only = true, 
};

static ToolResult read_exec(cJSON *args) {
    const char *rel_path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
    if (!rel_path) return (ToolResult){.ok = false, .output = xstrdup("missing 'path'")};

    char *safe_path = resolve_workspace_path(rel_path);
    if (!safe_path) return (ToolResult){.ok = false, .output = xstrdup("error: path is outside the workspace")};

    FILE *f = fopen(safe_path, "r");
    if (!f) {
        ToolResult res = {.ok = false, .output = xasprintf("Failed to open %s: %s", rel_path, strerror(errno))};
        free(safe_path);
        return res;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    rewind(f);

    if (size > MAX_FILE_SIZE) {
      fclose(f);
      free(safe_path);
      return (ToolResult){.ok = false,
                          .output = xasprintf("File too large (%ld bytes, max %d)",
                                              size, MAX_FILE_SIZE)};
    }

    char *buf = xmalloc(size + 1);
    size_t read_size = fread(buf, 1, size, f);
    buf[read_size] = '\0';
    
    fclose(f);
    free(safe_path);

    return (ToolResult){.ok = true, .output = buf};
}