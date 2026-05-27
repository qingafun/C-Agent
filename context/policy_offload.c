#include "context/internal.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define PREVIEW_LEN 200
#define MIN_OFFLOAD_BYTES 400
#define PATH_BUF (PATH_MAX + 256)

static bool offload_should_apply(Context *ctx) {
  return ctx_budget_usage(ctx) > g_config.offload_threshold;
}

static void ensure_offload_dir(void) {
  char path[PATH_BUF];
  snprintf(path, sizeof(path), "%s/.agent", g_config.workdir);
  mkdir(path, 0755);
  snprintf(path, sizeof(path), "%s/.agent/offload", g_config.workdir);
  mkdir(path, 0755);
}

static int offload_apply(Context *ctx, char *err, size_t err_cap) {
  (void)err;
  (void)err_cap;

  int keep = KEEP_RECENT_MSGS;
  int scan_end = ctx->history.len - keep;
  if (scan_end < 0)
    scan_end = 0;

  for (int i = 0; i < scan_end; i++) {
    cJSON *msg = cJSON_Parse(ctx->history.items[i]);
    if (!msg)
      continue;

    const char *role = json_str(msg, "role");
    const char *content = json_str(msg, "content");

    if (!role || strcmp(role, "tool") != 0 || !content) {
      cJSON_Delete(msg);
      continue;
    }

    size_t body_len = strlen(content);
    if (body_len <= MIN_OFFLOAD_BYTES) {
      cJSON_Delete(msg);
      continue;
    }

    ensure_offload_dir();
    char fpath[PATH_BUF];
    snprintf(fpath, sizeof(fpath), "%s/.agent/offload/%d.txt",
             g_config.workdir, ctx->next_offload_id);

    FILE *f = fopen(fpath, "w");
    if (!f) {
      cJSON_Delete(msg);
      continue;
    }
    fputs(content, f);
    fclose(f);

    size_t preview_chars = body_len < PREVIEW_LEN ? body_len : PREVIEW_LEN;
    char *replacement = xasprintf(
        "%.*s\n\n[Full content saved to .agent/offload/%d.txt. "
        "Use read_file to retrieve.]",
        (int)preview_chars, content, ctx->next_offload_id);

    cJSON_ReplaceItemInObject(msg, "content",
                              cJSON_CreateString(replacement));
    free(replacement);

    char *new_json = cJSON_PrintUnformatted(msg);
    ctx_replace_msg(ctx, i, new_json);

    cJSON_Delete(msg);
    ctx->next_offload_id++;
  }

  return 0;
}

ContextPolicy offload_policy = {
    .name = "offload",
    .should_apply = offload_should_apply,
    .apply = offload_apply,
};
