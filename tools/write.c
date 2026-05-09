#include "tools/tools.h"
#include "tools/sandbox.h"
#include "util.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

    /* Atomic write: write to temp file in same directory, then rename. */
    char tmp_path[PATH_MAX];
    snprintf(tmp_path, sizeof(tmp_path), "%s.XXXXXX", safe_path);
    int tmp_fd = mkstemp(tmp_path);
    if (tmp_fd < 0) {
      free(safe_path);
      return (ToolResult){.ok = false,
                          .output = xasprintf("Failed to create temp file: %s",
                                              strerror(errno))};
    }
    FILE *f = fdopen(tmp_fd, "w");
    if (!f) {
      close(tmp_fd);
      unlink(tmp_path);
      free(safe_path);
      return (ToolResult){.ok = false, .output = xstrdup("Failed to open temp file")};
    }

    fputs(content, f);
    fclose(f);

    if (rename(tmp_path, safe_path) != 0) {
      unlink(tmp_path);
      free(safe_path);
      return (ToolResult){.ok = false,
                          .output = xasprintf("Failed to save %s: %s", rel_path,
                                              strerror(errno))};
    }

    free(safe_path);
    return (ToolResult){.ok = true, .output = xasprintf("Successfully wrote to %s", rel_path)};
}