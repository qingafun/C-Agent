/*
 * The registry provides a unified interface for the LLM client to discover 
 * tool schemas and for the executor to resolve function pointers.
 * 
 * How to add a new tool:
 * 1. Create tools/new_tool.c and define a `ToolDef new_tool_def`.
 * 2. Add `extern ToolDef new_tool_def;` to the extern block below.
 * 3. Call `tool_register(&new_tool_def);` inside tools_init().
 */
#include "tools/tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ToolDef bash_def;
extern ToolDef read_file_def;
extern ToolDef write_file_def;
extern ToolDef edit_file_def;

static ToolDef *g_tools[MAX_REGISTERED_TOOLS];
static int g_tools_count = 0;

void tools_init(void) {
    if (g_tools_count > 0)
        return;

    tool_register(&bash_def);
    tool_register(&read_file_def);
    tool_register(&write_file_def);
    tool_register(&edit_file_def);
}

void tool_register(ToolDef *def) {
    if (g_tools_count >= MAX_REGISTERED_TOOLS) {
        fprintf(stderr, "Fatal: tool registry full (%d)\n", MAX_REGISTERED_TOOLS);
        exit(1);
    }
    g_tools[g_tools_count++] = def;
}

ToolDef *const *tool_list(int *out_count) {
    if (out_count)
        *out_count = g_tools_count;
    return g_tools;
}

ToolDef *tool_find(const char *name) {
    if (!name)
        return NULL;
    for (int i = 0; i < g_tools_count; i++) {
        if (strcmp(g_tools[i]->name, name) == 0)
            return g_tools[i];
    }
    return NULL;
}

void tool_result_free(ToolResult *r) {
    if (!r)
        return;
    free(r->output);
    r->output = NULL;
}
