#ifndef TOOLS_H
#define TOOLS_H

#include "cJSON.h"
#include "context/context.h"

#include <stdbool.h>

typedef struct {
  bool ok;
  char *output;
} ToolResult;

void tool_result_free(ToolResult *r);

#define MAX_TOOL_OUTPUT 50000

typedef ToolResult (*ToolFn)(cJSON *args);

typedef struct {
    const char *name;
    const char *desc;
    const char *param_schema;
    ToolFn exec;

    bool read_only;
} ToolDef;

#define MAX_REGISTERED_TOOLS 16

void tools_init(void);
void tool_register(ToolDef *def);
ToolDef *tool_find(const char *name);
ToolDef *const *tool_list(int *out_count);

/* ── extension tools ─────────────────────────────── */

extern ToolDef load_skill_def;
extern ToolDef save_memory_def;
extern ToolDef read_memory_def;
extern ToolDef save_session_def;
extern ToolDef load_session_def;
extern ToolDef list_sessions_def;
extern ToolDef run_subagent_def;

/* ── bash ─────────────────────────────────────────── */

extern ToolDef bash_def;

/* ── system prompt helpers ────────────────────────── */

char *skill_summary_text(void);
char *memory_summary_text(void);

/* ── session lifecycle ────────────────────────────── */

void session_set_context(struct Context *ctx);
void session_log_init(void);
void session_log_close(void);
void session_log_message(const char *json);
void session_log_pause(void);
void session_log_resume(void);
bool session_has_pending(void);
int  session_replay(void);

#endif
