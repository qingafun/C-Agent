/*
 * memory.c — Persistent project memory across sessions.
 *
 *   save_memory(key, content) — remembers a fact
 *   read_memory(key)          — recalls by key, or "_all" to list all
 *
 * Stored in .agent/memory/<key>.json relative to workspace.
 */

#include "tools/tools.h"
#include "tools/sandbox.h"
#include "config.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define MAX_MEMORY_SIZE (32 * 1024)

static void ensure_memory_dir(void) {
  char path[PATH_MAX + 256];
  snprintf(path, sizeof(path), "%s/.agent/memory", g_config.workdir);
  mkdir(path, 0755);
}

static char *safe_path_for_key(const char *key) {
  char rel[PATH_MAX];
  snprintf(rel, sizeof(rel), ".agent/memory/%s.json", key);
  return resolve_workspace_path(rel);
}

static ToolResult save_memory_exec(cJSON *args);

ToolDef save_memory_def = {
    .name = "save_memory",
    .desc = "Save a fact or decision into persistent project memory. "
            "Use this to remember project structure, build conventions, "
            "coding style, or decisions for future sessions.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"key\":{\"type\":\"string\","
                    "\"description\":\"Unique key for this memory entry\"},"
                    "\"content\":{\"type\":\"string\","
                    "\"description\":\"What to remember (can be multi-line)\"}"
                    "},"
                    "\"required\":[\"key\",\"content\"]}",
    .exec = save_memory_exec,
    .read_only = false,
};

static ToolResult save_memory_exec(cJSON *args) {
  const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(args, "key"));
  const char *content = cJSON_GetStringValue(
      cJSON_GetObjectItem(args, "content"));

  if (!key || strlen(key) == 0 || !content)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing key or content")};

  for (const char *p = key; *p; p++) {
    if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
          (*p >= '0' && *p <= '9') || *p == '_')) {
      return (ToolResult){.ok = false,
                          .output = xstrdup("key must be alphanumeric + underscore only")};
    }
  }

  ensure_memory_dir();
  char *safe_path = safe_path_for_key(key);
  if (!safe_path)
    return (ToolResult){.ok = false,
                        .output = xstrdup("invalid memory path")};

  time_t now = time(NULL);
  char ts_buf[32];
  strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%S", localtime(&now));

  cJSON *obj = cJSON_CreateObject();
  cJSON_AddStringToObject(obj, "key", key);
  cJSON_AddStringToObject(obj, "content", content);
  cJSON_AddStringToObject(obj, "timestamp", ts_buf);
  char *json = cJSON_PrintUnformatted(obj);
  cJSON_Delete(obj);

  FILE *f = fopen(safe_path, "w");
  free(safe_path);
  if (!f) {
    free(json);
    return (ToolResult){.ok = false,
                        .output = xasprintf("failed to write memory '%s'", key)};
  }
  fputs(json, f);
  fclose(f);
  free(json);

  return (ToolResult){.ok = true,
                      .output = xasprintf("Memory '%s' saved.", key)};
}

static ToolResult read_memory_exec(cJSON *args);

ToolDef read_memory_def = {
    .name = "read_memory",
    .desc = "Read a memory entry by key. Use key=\"_all\" to list all "
            "available memory keys with their timestamps.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"key\":{\"type\":\"string\","
                    "\"description\":\"Key to read, or '_all' to list all\"}"
                    "},"
                    "\"required\":[\"key\"]}",
    .exec = read_memory_exec,
    .read_only = true,
};

static ToolResult read_memory_exec(cJSON *args) {
  const char *key = cJSON_GetStringValue(cJSON_GetObjectItem(args, "key"));
  if (!key || strlen(key) == 0)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing key argument")};

  if (strcmp(key, "_all") == 0) {
    char dir_path[PATH_MAX + 256];
    snprintf(dir_path, sizeof(dir_path), "%s/.agent/memory",
             g_config.workdir);
    DIR *d = opendir(dir_path);
    if (!d)
      return (ToolResult){.ok = true,
                          .output = xstrdup("No memories saved yet.")};

    size_t cap = 1024, len = 0;
    char *buf = xmalloc(cap);
    buf[0] = '\0';
    len += (size_t)snprintf(buf, cap, "Memory keys:\n");

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      const char *nm = ent->d_name;
      size_t nl = strlen(nm);
      if (nl < 6 || strcmp(nm + nl - 5, ".json") != 0)
        continue;

      char fname[256];
      snprintf(fname, sizeof(fname), "%.*s", (int)(nl - 5), nm);

      char fpath[PATH_MAX + 256];
      snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, nm);
      FILE *f = fopen(fpath, "r");
      if (!f)
        continue;
      fseek(f, 0, SEEK_END);
      long sz = ftell(f);
      rewind(f);
      if (sz <= 0 || sz > MAX_MEMORY_SIZE) {
        fclose(f);
        continue;
      }
      char *raw = xmalloc((size_t)sz + 1);
      fread(raw, 1, (size_t)sz, f);
      raw[sz] = '\0';
      fclose(f);

      cJSON *obj = cJSON_Parse(raw);
      free(raw);
      if (!obj)
        continue;
      const char *ts = cJSON_GetStringValue(
          cJSON_GetObjectItem(obj, "timestamp"));

      size_t line = strlen(fname) + (ts ? strlen(ts) : 0) + 8;
      if (len + line + 1 > cap) {
        cap = (cap + line) * 2;
        buf = xrealloc(buf, cap);
      }
      len += (size_t)snprintf(buf + len, cap - len,
                               "  - %s (%s)\n",
                               fname, ts ? ts : "unknown");
      cJSON_Delete(obj);
    }
    closedir(d);
    return (ToolResult){.ok = true, .output = buf};
  }

  char *safe_path = safe_path_for_key(key);
  if (!safe_path)
    return (ToolResult){.ok = false,
                        .output = xstrdup("invalid memory key")};

  FILE *f = fopen(safe_path, "r");
  free(safe_path);
  if (!f)
    return (ToolResult){.ok = false,
                        .output = xasprintf("No memory found for '%s'", key)};

  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);
  if (sz <= 0 || sz > MAX_MEMORY_SIZE) {
    fclose(f);
    return (ToolResult){.ok = false, .output = xstrdup("memory file corrupt")};
  }

  char *raw = xmalloc((size_t)sz + 1);
  fread(raw, 1, (size_t)sz, f);
  raw[sz] = '\0';
  fclose(f);

  cJSON *obj = cJSON_Parse(raw);
  free(raw);
  if (!obj)
    return (ToolResult){.ok = false,
                        .output = xstrdup("failed to parse memory file")};

  const char *content = cJSON_GetStringValue(
      cJSON_GetObjectItem(obj, "content"));
  const char *ts = cJSON_GetStringValue(
      cJSON_GetObjectItem(obj, "timestamp"));

  char *out = xasprintf("[%s] %s: %s",
                        ts ? ts : "unknown", key,
                        content ? content : "(empty)");
  cJSON_Delete(obj);
  return (ToolResult){.ok = true, .output = out};
}

char *memory_summary_text(void) {
  char dir_path[PATH_MAX + 256];
  snprintf(dir_path, sizeof(dir_path), "%s/.agent/memory",
           g_config.workdir);

  DIR *d = opendir(dir_path);
  if (!d)
    return xstrdup("");

  size_t cap = 512, len = 0;
  char *buf = xmalloc(cap);
  buf[0] = '\0';

  int count = 0;
  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    const char *nm = ent->d_name;
    size_t nl = strlen(nm);
    if (nl < 6 || strcmp(nm + nl - 5, ".json") != 0)
      continue;

    if (count == 0) {
      len += (size_t)snprintf(buf + len, cap - len,
                              "\nPersistent memories from past sessions:\n");
    }
    count++;

    char fname[256];
    snprintf(fname, sizeof(fname), "%.*s", (int)(nl - 5), nm);

    size_t line = strlen(fname) + 6;
    if (len + line + 1 > cap) {
      cap = (cap + line) * 2;
      buf = xrealloc(buf, cap);
    }
    len += (size_t)snprintf(buf + len, cap - len,
                             "  - %s\n", fname);
  }
  closedir(d);

  if (count == 0) {
    free(buf);
    return xstrdup("");
  }
  return buf;
}
