#ifndef CONTEXT_INTERNAL_H
#define CONTEXT_INTERNAL_H

#include "cJSON.h"
#include "context/context.h"

#define MAX_POLICIES 8
#define KEEP_RECENT_MSGS 4

struct Context {
  MessageList history;
  int context_window;
  int next_offload_id;
  ContextPolicy *policies[MAX_POLICIES];
  int policy_count;
};

extern ContextPolicy offload_policy;
extern ContextPolicy summary_policy;

int ctx_estimate_tokens(const char *msg_json);
int ctx_total_tokens(const Context *ctx);

void ctx_replace_msg(Context *ctx, int index, char *new_json);
void ctx_replace_range(Context *ctx, int from, int to, char *new_json);

static inline const char *json_str(cJSON *obj, const char *key) {
  return cJSON_GetStringValue(cJSON_GetObjectItem(obj, key));
}

#endif
