#include "tools/tools.h"
#include "tools/sandbox.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ToolResult edit_exec(cJSON *args);

ToolDef edit_file_def = {
    .name = "edit_file",
    .desc = "Replace the FIRST occurrence of old_text with new_text in a file.",
    .param_schema = "{\"type\":\"object\","
    "\"properties\":{"
    "\"path\":{\"type\":\"string\"},"
    "\"old_text\":{\"type\":\"string\",\"description\":\"Exact string to be replaced\"},"
    "\"new_text\":{\"type\":\"string\",\"description\":\"New string to insert\"}"
    "},\"required\":[\"path\",\"old_text\",\"new_text\"]}",
    .exec = edit_exec,
    .read_only = false,
};

static ToolResult edit_exec(cJSON *args) {
    const char *rel_path = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
    const char *old_str = cJSON_GetStringValue(cJSON_GetObjectItem(args, "old_text"));
    const char *new_str = cJSON_GetStringValue(cJSON_GetObjectItem(args, "new_text"));
    
    if (!rel_path || !old_str || !new_str) 
        return (ToolResult){.ok = false, .output = xstrdup("missing required arguments")};

    char *safe_path = resolve_workspace_path(rel_path);
    if (!safe_path) return (ToolResult){.ok = false, .output = xstrdup("error: path is outside the workspace")};

    FILE *f = fopen(safe_path, "r");
    if (!f) { free(safe_path); return (ToolResult){.ok = false, .output = xstrdup("File not found")}; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *content = xmalloc(size + 1);
    fread(content, 1, size, f);
    content[size] = '\0';
    fclose(f);

    char *pos = strstr(content, old_str);
    if (!pos) {
        free(content); free(safe_path);
        return (ToolResult){.ok = false, .output = xstrdup("Error: old_text not found in file")};
    }

    size_t prefix_len = pos - content;
    size_t old_len = strlen(old_str);
    size_t new_len = strlen(new_str);
    size_t suffix_len = strlen(pos + old_len);
    
    char *new_content = xmalloc(prefix_len + new_len + suffix_len + 1);
    memcpy(new_content, content, prefix_len);
    memcpy(new_content + prefix_len, new_str, new_len);
    memcpy(new_content + prefix_len + new_len, pos + old_len, suffix_len);
    new_content[prefix_len + new_len + suffix_len] = '\0';

    f = fopen(safe_path, "w");
    if (f) {
        fputs(new_content, f);
        fclose(f);
    }

    free(content); free(new_content); free(safe_path);
    return (ToolResult){.ok = true, .output = xasprintf("Successfully updated %s", rel_path)};
}