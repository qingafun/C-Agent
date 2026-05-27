/*
 * session.c — Persistent conversation log with crash recovery.
 *
 *   Auto-save: every message pushed to context is appended to
 *     .agent/sessions/current.jsonl (JSONL format).
 *   Replay: on startup, current.jsonl is replayed into context.
 *   Tools: save_session(name), load_session(name), list_sessions().
 */

#include "tools/tools.h"
#include "config.h"
#include "context/context.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static FILE *g_session_log = NULL;
static bool   g_log_enabled = true;
static Context *g_ctx = NULL;

static void ensure_session_dir(void) {
  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions", g_config.workdir);
  mkdir(path, 0755);
}

static const char *session_dir(void) {
  static char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions", g_config.workdir);
  return path;
}

void session_set_context(Context *ctx) { g_ctx = ctx; }

void session_log_init(void) {
  ensure_session_dir();
  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions/current.jsonl",
           g_config.workdir);
  g_session_log = fopen(path, "a");
}

static void session_log_close_internal(void) {
  if (g_session_log) {
    fclose(g_session_log);
    g_session_log = NULL;
  }
}

void session_log_close(void) {
  session_log_close_internal();
  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions/current.jsonl",
           g_config.workdir);
  remove(path);
}

void session_log_message(const char *json) {
  if (!g_log_enabled || !g_session_log || !json) return;
  fputs(json, g_session_log);
  fputc('\n', g_session_log);
  fflush(g_session_log);
}

void session_log_pause(void)  { g_log_enabled = false; }
void session_log_resume(void) { g_log_enabled = true; }

bool session_has_pending(void) {
  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions/current.jsonl",
           g_config.workdir);
  struct stat st;
  return stat(path, &st) == 0 && st.st_size > 0;
}

int session_replay(void) {
  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions/current.jsonl",
           g_config.workdir);

  struct stat st;
  if (stat(path, &st) != 0 || st.st_size == 0)
    return 0;

  FILE *f = fopen(path, "r");
  if (!f) return 0;

  session_log_pause();

  char *line = NULL;
  size_t line_cap = 0;
  int count = 0;
  while (getline(&line, &line_cap, f) != -1) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len == 0) continue;
    ctx_push(g_ctx, xstrdup(line));
    count++;
  }
  free(line);
  fclose(f);

  session_log_resume();

  session_log_close_internal();
  session_log_init();

  if (count > 0)
    printf("[session] replayed %d messages from previous session\n", count);
  return count;
}

static ToolResult save_session_exec(cJSON *args);

ToolDef save_session_def = {
    .name = "save_session",
    .desc = "Save the current conversation to a named session. "
            "The session can be restored later with load_session.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"name\":{\"type\":\"string\","
                    "\"description\":\"Name for this saved session\"}"
                    "},"
                    "\"required\":[\"name\"]}",
    .exec = save_session_exec,
    .read_only = false,
};

static ToolResult save_session_exec(cJSON *args) {
  const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
  if (!name || strlen(name) == 0)
    return (ToolResult){.ok = false, .output = xstrdup("missing session name")};

  for (const char *p = name; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_')) {
      return (ToolResult){.ok = false,
                          .output = xstrdup("name must be alphanumeric + underscore only")};
    }
  }

  ensure_session_dir();

  session_log_close_internal();

  char src[PATH_MAX + 256];
  char dst[PATH_MAX + 256];
  snprintf(src, sizeof(src), "%s/.agent/sessions/current.jsonl", g_config.workdir);
  snprintf(dst, sizeof(dst), "%s/.agent/sessions/%s.jsonl", g_config.workdir, name);

  FILE *fin = fopen(src, "r");
  FILE *fout = fopen(dst, "w");
  bool ok = true;
  if (fin && fout) {
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), fin)) > 0)
      fwrite(buf, 1, n, fout);
  } else {
    ok = false;
  }
  if (fin)  fclose(fin);
  if (fout) fclose(fout);

  session_log_init();

  if (!ok)
    return (ToolResult){.ok = false,
                        .output = xasprintf("failed to save session '%s'", name)};

  return (ToolResult){.ok = true,
                      .output = xasprintf("Session saved as '%s'.", name)};
}

static ToolResult load_session_exec(cJSON *args);

ToolDef load_session_def = {
    .name = "load_session",
    .desc = "Load a saved session, restoring its conversation history.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"name\":{\"type\":\"string\","
                    "\"description\":\"Name of the session to load\"}"
                    "},"
                    "\"required\":[\"name\"]}",
    .exec = load_session_exec,
    .read_only = false,
};

static ToolResult load_session_exec(cJSON *args) {
  const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
  if (!name || strlen(name) == 0)
    return (ToolResult){.ok = false, .output = xstrdup("missing session name")};

  for (const char *p = name; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_')) {
      return (ToolResult){.ok = false,
                          .output = xstrdup("name must be alphanumeric + underscore only")};
    }
  }

  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/sessions/%s.jsonl",
           g_config.workdir, name);

  struct stat st;
  if (stat(path, &st) != 0 || st.st_size == 0)
    return (ToolResult){.ok = false,
                        .output = xasprintf("Session '%s' not found.", name)};

  FILE *f = fopen(path, "r");
  if (!f)
    return (ToolResult){.ok = false,
                        .output = xasprintf("Cannot open session '%s'.", name)};

  session_log_close();

  session_log_pause();
  ctx_clear(g_ctx);

  char *line = NULL;
  size_t line_cap = 0;
  int count = 0;
  while (getline(&line, &line_cap, f) != -1) {
    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';
    if (len == 0) continue;
    ctx_push(g_ctx, xstrdup(line));
    count++;
  }
  free(line);
  fclose(f);

  session_log_resume();

  char cur_path[PATH_MAX + 256];
  snprintf(cur_path, sizeof(cur_path), "%s/.agent/sessions/current.jsonl",
           g_config.workdir);

  FILE *fout = fopen(cur_path, "w");
  if (fout) {
    f = fopen(path, "r");
    if (f) {
      char buf[8192];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        fwrite(buf, 1, n, fout);
      fclose(f);
    }
    fclose(fout);
  }

  session_log_init();

  return (ToolResult){.ok = true,
                      .output = xasprintf("Session '%s' loaded (%d messages).",
                                          name, count)};
}

static ToolResult list_sessions_exec(cJSON *args);

ToolDef list_sessions_def = {
    .name = "list_sessions",
    .desc = "List all saved session names.",
    .param_schema = "{\"type\":\"object\",\"properties\":{}}",
    .exec = list_sessions_exec,
    .read_only = true,
};

static ToolResult list_sessions_exec(cJSON *args) {
  (void)args;
  ensure_session_dir();

  DIR *d = opendir(session_dir());
  if (!d)
    return (ToolResult){.ok = true, .output = xstrdup("No sessions saved.")};

  size_t cap = 256, len = 0;
  char *buf = xmalloc(cap);
  buf[0] = '\0';
  len += (size_t)snprintf(buf, cap, "Saved sessions:\n");

  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    const char *nm = ent->d_name;
    size_t nl = strlen(nm);
    if (nl < 7 || strcmp(nm + nl - 6, ".jsonl") != 0) continue;
    if (strncmp(nm, "current", 7) == 0 && nl == 13) continue;

    char fname[256];
    snprintf(fname, sizeof(fname), "%.*s", (int)(nl - 6), nm);
    size_t line = strlen(fname) + 6;
    if (len + line + 1 > cap) {
      cap = (cap + line) * 2;
      buf = xrealloc(buf, cap);
    }
    len += (size_t)snprintf(buf + len, cap - len, "  - %s\n", fname);
    count++;
  }
  closedir(d);

  if (count == 0) {
    free(buf);
    return (ToolResult){.ok = true, .output = xstrdup("No sessions saved.")};
  }
  return (ToolResult){.ok = true, .output = buf};
}
