/*
 * subagent.c — Spawn child agents for independent subtasks.
 *
 *   run_subagent(task) — creates a child agent with its own context window,
 *   executes the task, and returns a structured result.
 */

#include "tools/tools.h"
#include "agent/agent.h"
#include "config.h"
#include "context/context.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SUBAGENT_OUTPUT (16 * 1024)

static ToolResult run_subagent_exec(cJSON *args);

ToolDef run_subagent_def = {
    .name = "run_subagent",
    .desc = "Spawn a child agent to handle an independent subtask. "
            "The child gets its own context window, executes the task, "
            "and returns a structured result. Use this for parallelizable "
            "subtasks like searching, reading, or building.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"task\":{\"type\":\"string\","
                    "\"description\":\"Task description for the child agent\"}"
                    "},"
                    "\"required\":[\"task\"]}",
    .exec = run_subagent_exec,
    .read_only = true,
};

static ToolResult run_subagent_exec(cJSON *args) {
  const char *task = cJSON_GetStringValue(cJSON_GetObjectItem(args, "task"));
  if (!task || strlen(task) == 0)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing task description")};

  int child_window = g_config.context_window / 2;
  if (child_window < 1000) child_window = 1000;

  session_log_pause();

  Agent *child = agent_create(child_window);
  if (!child) {
    session_log_resume();
    return (ToolResult){.ok = false,
                        .output = xstrdup("failed to create subagent")};
  }

  const char *reply = agent_chat(child, task);

  ToolResult result;
  if (reply && strlen(reply) > 0) {
    size_t len = strlen(reply);
    if (len > MAX_SUBAGENT_OUTPUT) {
      result.ok = true;
      result.output = xasprintf(
          "Subagent result (truncated):\n%.*s\n...[truncated]",
          MAX_SUBAGENT_OUTPUT, reply);
    } else {
      result.ok = true;
      result.output = xasprintf("Subagent result:\n%s", reply);
    }
  } else {
    result.ok = false;
    result.output = xstrdup("Subagent did not produce a result.");
  }

  agent_free(child);
  session_log_resume();

  return result;
}
