#ifndef CONTEXT_H
#define CONTEXT_H

#include "message.h"

#include <stdbool.h>
#include <stddef.h>

typedef struct Context Context;

typedef struct {
  const char *name;
  bool (*should_apply)(Context *ctx);
  int (*apply)(Context *ctx, char *err, size_t err_cap);
} ContextPolicy;

Context *ctx_create(int context_window);
void ctx_free(Context *ctx);

void ctx_add_policy(Context *ctx, ContextPolicy *policy);

extern ContextPolicy offload_policy;
extern ContextPolicy summary_policy;

void ctx_push(Context *ctx, char *msg_json);
void ctx_clear(Context *ctx);

int ctx_reclaim(Context *ctx, char *err, size_t err_cap);

float ctx_budget_usage(const Context *ctx);
const MessageList *ctx_history(const Context *ctx);

#endif
