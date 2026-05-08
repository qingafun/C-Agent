#include "tools/tools.h"
#include "tools/sandbox.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ToolResult write_exec(cJSON *args);

ToolDef write_file_def = {
    .name = "write_file",
    .desc = "Write full content to a file, overwriting existing content or creating a new file.",
    .param_schema =     "{\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"content\":{\"type\":\"string\",\"description\":\"The complete file content to write\"}"
    "},\"required\":[\"path\",\"content\"]}",
    .exec = write_exec,
    .read_only = false,
};

static ToolResult write_exec(cJSON *args) {
    const char *rel_path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
    const char *content = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
    if (!rel_path || !content) return (ToolResult){.ok = false, .output = xstrdup("missing 'path' or 'content'")};

    char *safe_path = resolve_workspace_path(rel_path);
    if (!safe_path) return (ToolResult){.ok = false, .output = xstrdup("error: path is outside the workspace")};

    FILE *f = fopen(safe_path, "w");
    if (!f) {
        ToolResult res = {.ok = false, .output = xasprintf("Failed to write %s: %s", rel_path, strerror(errno))};
        free(safe_path);
        return res;
    }

    fputs(content, f);
    fclose(f);
    free(safe_path);

    return (ToolResult){.ok = true, .output = xasprintf("Successfully wrote to %s", rel_path)};
}