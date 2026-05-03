#ifndef TOOLS_H
#define TOOLS_H

#include "cJSON.h"

#include <stdbool.h>

/*
 * tools.h — Tool interface for the AI Agent.
 *
 * Defines the common result type for all tools and exposes the concrete
 * "bash" tool implementation. This minimal interface intentionally avoids
 * a registry abstraction; the agent dispatches tools by name directly.
 * (A more generic registry will be added in a future iteration.)
 */

/*
 * Result returned by a tool after execution.
 * - ok: whether the command exited cleanly (success)
 * - output: captured stdout/stderr (malloc'd, caller must free)
 */

typedef struct {
  bool ok;      /* command exited cleanly */
  char *output; /* malloc'd, may be NULL ("no output" case) */
} ToolResult;

void tool_result_free(ToolResult *r);

/* ── bash ─────────────────────────────────────────── */

/* Tool schema fields — referenced by llm_client when building the request. */
extern const char *BASH_TOOL_NAME;
extern const char *BASH_TOOL_DESC;
extern const char *BASH_TOOL_SCHEMA; /* JSON Schema fragment as a raw string */

ToolResult bash_tool_exec(cJSON *args);

#endif
