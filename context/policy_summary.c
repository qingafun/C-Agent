#include "context/internal.h"
#include "config.h"
#include "agent/llm_client.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SUMMARY_BATCH 20

static bool summary_should_apply(Context *ctx) {
  return ctx_budget_usage(ctx) > g_config.summary_threshold;
}

static int summary_apply(Context *ctx, char *err, size_t err_cap) {
  int keep = KEEP_RECENT_MSGS;
  int total = ctx->history.len;

  if (total <= keep)
    return 0;

  int batch_end = total - keep;
  if (batch_end > SUMMARY_BATCH)
    batch_end = SUMMARY_BATCH;

  size_t concat_cap = 4096;
  size_t concat_len = 0;
  char *concat = xmalloc(concat_cap);
  concat[0] = '\0';

  for (int i = 0; i < batch_end; i++) {
    cJSON *msg = cJSON_Parse(ctx->history.items[i]);
    if (!msg)
      continue;
    const char *role = json_str(msg, "role");
    const char *content = json_str(msg, "content");
    if (!role)
      role = "unknown";
    if (!content)
      content = "";

    size_t line_len = strlen(role) + strlen(content) + 16;
    if (concat_len + line_len + 1 > concat_cap) {
      concat_cap = (concat_cap + line_len) * 2;
      concat = xrealloc(concat, concat_cap);
    }

    concat_len += (size_t)snprintf(concat + concat_len,
                                    concat_cap - concat_len,
                                    "[%s]: %s\n", role, content);
    cJSON_Delete(msg);
  }

  cJSON *wrap = cJSON_CreateObject();
  cJSON_AddStringToObject(wrap, "role", "user");
  cJSON_AddStringToObject(wrap, "content", concat);
  char *wrap_json = cJSON_PrintUnformatted(wrap);
  cJSON_Delete(wrap);

  MessageList batch;
  msg_list_init(&batch);
  msg_list_push(&batch, wrap_json);

  const char *sys_prompt =
      "You are a conversation summarizer. Below is a prefix of an ongoing "
      "agent conversation. Produce a concise summary (plain text, no JSON) "
      "that preserves: the user's goal, key decisions made, facts discovered, "
      "and current progress. Omit verbatim tool output and redundant details.";

  LLMResponse resp;
  memset(&resp, 0, sizeof(resp));
  int rc = llm_chat(&batch, sys_prompt, g_config.model, &resp, err, err_cap);

  msg_list_free(&batch);
  free(concat);

  if (rc != 0)
    return -1;

  if (!resp.content || resp.content[0] == '\0') {
    llm_response_free(&resp);
    snprintf(err, err_cap, "summary policy: LLM returned empty content");
    return -1;
  }

  cJSON *summary_obj = cJSON_CreateObject();
  cJSON_AddStringToObject(summary_obj, "role", "user");
  cJSON_AddStringToObject(summary_obj, "content", resp.content);
  char *summary_json = cJSON_PrintUnformatted(summary_obj);
  cJSON_Delete(summary_obj);

  ctx_replace_range(ctx, 0, batch_end, summary_json);
  llm_response_free(&resp);
  return 0;
}

ContextPolicy summary_policy = {
    .name = "summary",
    .should_apply = summary_should_apply,
    .apply = summary_apply,
};
