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
#include "context/context.h"
#include "llm_client.h"
#include "message.h"
#include "tools/tools.h"
#include "tools/executor.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui/ui.h"

static const char AGENT_SYSTEM_TEMPLATE[] =
    "You are a coding agent running in the CLI. Your workspace is %s.\n"
    "All read_file/write_file/edit_file path arguments MUST be relative to "
    "the workspace root. Absolute paths (starting with /) are rejected.\n"
    "When using the 'bash' tool, you MUST provide arguments in this EXACT "
    "JSON format:\n"
    "{\"command\": \"your_command_here\"}\n"
    "Return a short, final text reply when the task is done.";

struct Agent {
  char *system_prompt;
  char *last_reply;
  Context *ctx;
};

Agent *agent_create(int context_window) {
  Agent *a = xmalloc(sizeof(*a));
  memset(a, 0, sizeof(*a));

  char *base = xasprintf(AGENT_SYSTEM_TEMPLATE, g_config.workdir);
  char *skills = skill_summary_text();
  char *mem = memory_summary_text();
  a->system_prompt = xasprintf("%s%s%s", base, skills, mem);
  free(base);
  free(skills);
  free(mem);

  a->ctx = ctx_create(context_window);
  ctx_add_policy(a->ctx, &offload_policy);
  ctx_add_policy(a->ctx, &summary_policy);

  return a;
}

Context *agent_get_context(Agent *a) { return a ? a->ctx : NULL; }

void agent_free(Agent *a) {
  if (!a)
    return;
  free(a->system_prompt);
  free(a->last_reply);
  ctx_free(a->ctx);
  free(a);
}

const char *agent_chat(Agent *a, const char *user_input) {
  char *user_msg = msg_user_json(user_input);
  ctx_push(a->ctx, user_msg);
  session_log_message(user_msg);

  int max_turns = 15;

  for (int turn = 0; turn < max_turns; turn++) {
    LLMResponse resp;
    char err[256] = {0};

    char reclaim_err[256] = {0};
    if (ctx_reclaim(a->ctx, reclaim_err, sizeof(reclaim_err)) != 0) {
      fprintf(stderr, "agent_chat: context reclaim failed - %s\n", reclaim_err);
      return NULL;
    }

    ui_begin_thinking();
    int rc = llm_chat(ctx_history(a->ctx), a->system_prompt, g_config.model,
                      &resp, err, sizeof(err));
    ui_idle();

    if (rc != 0) {
      fprintf(stderr, "agent_chat: llm_chat failed - %s\n", err);
      return NULL;
    }

    char *assistant_msg = xstrdup(resp.raw_message);
    ctx_push(a->ctx, assistant_msg);
    session_log_message(assistant_msg);

    if (resp.n_tool_calls == 0) {
      free(a->last_reply);
      a->last_reply = xstrdup(resp.content);
      llm_response_free(&resp);
      return a->last_reply;
    }

    char **tool_msgs = xmalloc(resp.n_tool_calls * sizeof(char *));
    memset(tool_msgs, 0, resp.n_tool_calls * sizeof(char *));
    char err_buf[256] = {0};

    int rc_exec = executor_run_tools(resp.tool_calls, resp.n_tool_calls,
                                      tool_msgs, err_buf, sizeof(err_buf));

    if (rc_exec == 0) {
      for (int i = 0; i < resp.n_tool_calls; i++) {
        if (tool_msgs[i]) {
          ctx_push(a->ctx, tool_msgs[i]);
          session_log_message(tool_msgs[i]);
        }
      }
    } else {
      fprintf(stderr, "agent_chat: executor failed - %s\n", err_buf);
    }
    free(tool_msgs);

    llm_response_free(&resp);
  }

  fprintf(stderr, "agent_chat: max turns exceeded.\n");
  return NULL;
}
