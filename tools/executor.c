/*
 * This module acts as the crucial bridge between the LLM's reasoning layer 
 * and the local operating system environment. It is responsible for parsing, 
 * scheduling, and executing tool calls (e.g., bash, read, write) safely.
 */
#include "tools/executor.h"

#include "message.h"
#include "tools/tools.h"
#include "ui/ui.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

static bool g_approve_all = false;

static const char *summarize_tool(const LLMToolCall *call) {
  static char buf[256];
  if (!call || !call->name) return "";
  cJSON *args = call->args;
  const char *name = call->name;

  if (strcmp(name, "write_file") == 0) {
    const char *p = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
    const char *c = cJSON_GetStringValue(cJSON_GetObjectItem(args, "content"));
    snprintf(buf, sizeof(buf), "write %s (%zu bytes)",
             p ? p : "?", c ? strlen(c) : 0);
  } else if (strcmp(name, "edit_file") == 0) {
    const char *p = cJSON_GetStringValue(cJSON_GetObjectItem(args, "path"));
    const char *o = cJSON_GetStringValue(cJSON_GetObjectItem(args, "old_text"));
    snprintf(buf, sizeof(buf), "edit %s (%zu bytes)",
             p ? p : "?", o ? strlen(o) : 0);
  } else if (strcmp(name, "bash") == 0) {
    const char *cmd = cJSON_GetStringValue(cJSON_GetObjectItem(args, "command"));
    if (cmd && cmd[0]) {
      if (strlen(cmd) > 80)
        snprintf(buf, sizeof(buf), "bash: %.*s...", 80, cmd);
      else
        snprintf(buf, sizeof(buf), "bash: %s", cmd);
    } else {
      snprintf(buf, sizeof(buf), "bash: (empty)");
    }
  } else {
    snprintf(buf, sizeof(buf), "%s", name);
  }
  return buf;
}

static bool confirm_tool(const LLMToolCall *call) {
  if (g_approve_all) return true;

  printf("\n  [y/n/a] %s? ", summarize_tool(call));
  fflush(stdout);

  char answer[16];
  if (!fgets(answer, sizeof(answer), stdin)) return false;
  size_t n = strlen(answer);
  if (n > 0 && answer[n - 1] == '\n') answer[n - 1] = '\0';

  if (strcmp(answer, "a") == 0) { g_approve_all = true; return true; }
  return answer[0] == 'y' || answer[0] == 'Y';
}

typedef struct {
    LLMToolCall *call;
    ToolDef *def;
    ToolResult result;
    int index;
} ToolTask;

static void run_one(ToolTask *task) {
    if (task->def) {
        task->result = task->def->exec(task->call->args);
    } else {
        task->result = (ToolResult){
            .ok = false,
            .output = xasprintf("unknown tool: %s", task->call->name ? task->call->name : "(null)"),
        };
    }
    ui_tool_done(task->index, task->result.ok, task->result.output);
}

static void *thread_worker(void *arg) {
    ToolTask *task = (ToolTask *)arg;
    run_one(task);
    return NULL;
}

int executor_run_tools(
    LLMToolCall tool_calls[], int count, char *out_msgs[], char *err, size_t err_cap
) {
    if (count <= 0)
        return 0;

    ToolTask *tasks = xmalloc((size_t)count * sizeof(*tasks));
    ToolCallView *views = xmalloc((size_t)count * sizeof(*views));
    char **args_json = xmalloc((size_t)count * sizeof(*args_json));

    memset(out_msgs, 0, (size_t)count * sizeof(*out_msgs));
    for (int i = 0; i < count; i++) {
        tasks[i].call = &tool_calls[i];
        tasks[i].def = tool_find(tool_calls[i].name);
        tasks[i].index = i;
        tasks[i].result = (ToolResult){0};

        args_json[i] = cJSON_PrintUnformatted(tool_calls[i].args);
        if (!args_json[i]) {
            snprintf(err, err_cap, "failed to serialize tool call arguments");
            for (int j = 0; j < i; j++)
                free(args_json[j]);
            free(args_json);
            free(views);
            free(tasks);
            return -1;
        }
        views[i].name = tool_calls[i].name;
        views[i].args_display = args_json[i];
    }

    /* Confirm non-read-only tools before spinning up the UI.
       agent.c called ui_idle() before entering us, so stdout is safe. */
    bool *declined = calloc((size_t)count, sizeof(bool));
    for (int i = 0; i < count; i++) {
      if (tasks[i].def && !tasks[i].def->read_only && !confirm_tool(tasks[i].call))
        declined[i] = true;
    }

    ui_begin_tools(count, views);

    bool can_run_parallel = true;
    
    if (count >= 2) {
        for (int i = 0; i < count; i++) {
            if (tasks[i].def == NULL || tasks[i].def->read_only == false) {
                can_run_parallel = false;
                break;
            }
        }
    } else {
        can_run_parallel = false;
    }

    if (can_run_parallel) {
        pthread_t *threads = xmalloc((size_t)count * sizeof(*threads));
        
        for (int i = 0; i < count; i++) {
            pthread_create(&threads[i], NULL, thread_worker, &tasks[i]);
        }
        
        for (int i = 0; i < count; i++) {
            pthread_join(threads[i], NULL);
        }
        free(threads);
    } else {
        for (int i = 0; i < count; i++) {
            if (declined[i]) {
              tasks[i].result = (ToolResult){
                  .ok = false,
                  .output = xstrdup("User declined to execute"),
              };
              ui_tool_done(tasks[i].index, false, tasks[i].result.output);
              continue;
            }
            run_one(&tasks[i]);
        }
    }

    ui_idle();

    int rc = 0;
    for (int i = 0; i < count; i++) {
        const char *content = tasks[i].result.output ? tasks[i].result.output : "(no output)";
        out_msgs[i] = msg_tool_json(tasks[i].call->id ? tasks[i].call->id : "", content);
        if (!out_msgs[i]) {
            snprintf(err, err_cap, "failed to serialize tool result");
            rc = -1;
            break;
        }
    }
    if (rc != 0) {
        for (int i = 0; i < count; i++) {
            free(out_msgs[i]);
            out_msgs[i] = NULL;
        }
    }

    for (int i = 0; i < count; i++) {
        free(args_json[i]);
        tool_result_free(&tasks[i].result);
    }
    free(args_json);
    free(views);
    free(declined);
    free(tasks);
    return rc;
}
