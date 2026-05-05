/*
 * agent.c — Core ReAct loop orchestrating user input, LLM inference, and tool execution.
 *
 * The agent follows an iterative reasoning-acting loop:
 *   1. Append user message to conversation history
 *   2. Query LLM with full history and available tools
 *   3. If LLM returns tool calls, execute them in order and feed results back
 *   4. Repeat until LLM produces a final answer or max turns exceeded
 */
#include "agent.h"

#include "config.h"
#include "llm_client.h"
#include "message.h"
#include "tools/tools.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/ui.h"

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI at %s.\n"
    "Use the provided tools when you need to run shell commands.\n"
    "Return a short, final text reply when the task is done.";

struct Agent {
  char *system_prompt;
  char *last_reply;
  MessageList history;
};

Agent *agent_create(void) {
  Agent *a = calloc(1, sizeof(*a));
  if (!a)
    return NULL;
  a->system_prompt = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
  msg_list_init(&a->history);

  return a;
}

void agent_free(Agent *a) {
  if (!a)
    return;
  free(a->system_prompt);
  free(a->last_reply);
  msg_list_free(&a->history);
  free(a);
}

const char *agent_chat(Agent *a, const char *user_input) {
  msg_list_push(&a->history, msg_user_json(user_input));

  int max_turns = 15;

  for (int turn = 0; turn < max_turns; turn++) {
    LLMResponse resp;
    char err[256] = {0};

    // Initialize UI
    ui_begin_thinking(); 
    int rc = llm_chat(&a->history, a->system_prompt, g_config.model, &resp, err, sizeof(err));
    ui_idle(); // Wait for output

    if (rc != 0) {
      fprintf(stderr, "agent_chat: llm_chat failed - %s\n", err);
      return NULL;
    }

    msg_list_push(&a->history, xstrdup(resp.raw_message));

    // Already get final answer.

    // Return text
    if (resp.n_tool_calls == 0) {
      free(a->last_reply);
      a->last_reply = xstrdup(resp.content); 
      llm_response_free(&resp);
      return a->last_reply;
    }

    // Return tools execution
    ToolCallView *views = xmalloc(sizeof(ToolCallView) * resp.n_tool_calls);
    for (int i = 0; i < resp.n_tool_calls; i++) {
        views[i].name = resp.tool_calls[i].name;
        views[i].args_display = NULL;
    }
    ui_begin_tools(resp.n_tool_calls, views);

    for (int i = 0; i < resp.n_tool_calls; i++) {
      LLMToolCall *tc = &resp.tool_calls[i];
      ToolResult tr;

      if (strcmp(tc->name, "bash") == 0) {
        tr = bash_tool_exec(tc->args);
      }
      else {
        // Unknown tools
        tr.ok = false;
        tr.output = xasprintf("Unknown tool requested: %s", tc->name);
      }

      ui_tool_done(i, tr.ok, tr.output);

      msg_list_push(&a->history, msg_tool_json(tc->id, tr.output));
      tool_result_free(&tr);
    }

    llm_response_free(&resp);
  }

  fprintf(stderr, "agent_chat: max turns exceeded.\n");
  return NULL;
}
