#ifndef TOOLS_EXECUTOR_H
#define TOOLS_EXECUTOR_H

#include "agent/llm_client.h"

#include <stddef.h>

/*
 * Run every tool the LLM asked for in one assistant message.
 *
 * Inputs:
 *   tool_calls   array of LLMToolCall, in the order the LLM emitted them
 *   count        size of that array (0 < count <= MAX_TOOL_CALLS)
 *
 * Outputs:
 *   out_msgs     array of `count` malloc'd strings; on success out_msgs[i]
 *                is a serialized {"role":"tool",...} JSON message ready to
 *                push into history. Caller takes ownership.
 *
 *   err          error buffer, written only on failure
 */
int executor_run_tools(
    LLMToolCall tool_calls[], int count, char *out_msgs[], char *err, size_t err_cap
);

#endif /* TOOLS_EXECUTOR_H */
