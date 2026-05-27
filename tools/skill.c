#include "tools/tools.h"
#include "tools/sandbox.h"
#include "config.h"
#include "util.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SKILL_SIZE (64 * 1024)

static ToolResult load_skill_exec(cJSON *args);

ToolDef load_skill_def = {
    .name = "load_skill",
    .desc = "Load the full prompt for a skill by name. Use this when a skill "
            "seems relevant to the current task. Returns the complete "
            "instructions the agent should follow.",
    .param_schema = "{\"type\":\"object\","
                    "\"properties\":{"
                    "\"name\":{\"type\":\"string\","
                    "\"description\":\"Name of the skill to load (e.g. "
                    "code_review, debug, refactor)\"}"
                    "},"
                    "\"required\":[\"name\"]}",
    .exec = load_skill_exec,
    .read_only = true,
};

static ToolResult load_skill_exec(cJSON *args) {
  const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(args, "name"));
  if (!name || strlen(name) == 0)
    return (ToolResult){.ok = false,
                        .output = xstrdup("missing required argument: name")};

  char rel[PATH_MAX];
  snprintf(rel, sizeof(rel), ".agent/skills/%s.json", name);

  char *safe_path = resolve_workspace_path(rel);
  if (!safe_path)
    return (ToolResult){.ok = false,
                        .output = xstrdup("error: skill path invalid")};

  FILE *f = fopen(safe_path, "r");
  free(safe_path);
  if (!f)
    return (ToolResult){.ok = false,
                        .output = xasprintf("skill '%s' not found", name)};

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);

  if (size <= 0 || size > MAX_SKILL_SIZE) {
    fclose(f);
    return (ToolResult){.ok = false,
                        .output = xstrdup("skill file empty or too large")};
  }

  char *raw = xmalloc((size_t)size + 1);
  fread(raw, 1, (size_t)size, f);
  raw[size] = '\0';
  fclose(f);

  cJSON *skill_obj = cJSON_Parse(raw);
  free(raw);

  if (!skill_obj)
    return (ToolResult){.ok = false,
                        .output = xstrdup("failed to parse skill file")};

  const char *prompt = cJSON_GetStringValue(
      cJSON_GetObjectItem(skill_obj, "prompt"));

  if (!prompt) {
    cJSON_Delete(skill_obj);
    return (ToolResult){.ok = false,
                        .output = xstrdup("skill file missing 'prompt' field")};
  }

  char *output = xstrdup(prompt);
  cJSON_Delete(skill_obj);
  return (ToolResult){.ok = true, .output = output};
}

char *skill_summary_text(void) {
  char dir_path[PATH_MAX];
  snprintf(dir_path, sizeof(dir_path), "%s/.agent/skills",
           g_config.workdir);

  DIR *d = opendir(dir_path);
  if (!d)
    return xstrdup("");

  size_t cap = 1024;
  size_t len = 0;
  char *buf = xmalloc(cap);
  buf[0] = '\0';

  len += (size_t)snprintf(buf + len, cap - len,
                          "\nAvailable skills (use load_skill to get full "
                          "instructions):\n");

  struct dirent *ent;
  while ((ent = readdir(d)) != NULL) {
    const char *nm = ent->d_name;
    size_t nm_len = strlen(nm);
    if (nm_len < 6 || strcmp(nm + nm_len - 5, ".json") != 0)
      continue;

    char fpath[PATH_MAX];
    snprintf(fpath, sizeof(fpath), "%s/%s", dir_path, nm);

    FILE *f = fopen(fpath, "r");
    if (!f)
      continue;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    if (sz <= 0 || sz > MAX_SKILL_SIZE) {
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

    const char *name = cJSON_GetStringValue(
        cJSON_GetObjectItem(obj, "name"));
    const char *desc = cJSON_GetStringValue(
        cJSON_GetObjectItem(obj, "description"));

    if (name && desc) {
      size_t line_len = strlen(name) + strlen(desc) + 8;
      if (len + line_len + 1 > cap) {
        cap = (cap + line_len) * 2;
        buf = xrealloc(buf, cap);
      }
      len += (size_t)snprintf(buf + len, cap - len,
                               "  - %s: %s\n", name, desc);
    }
    cJSON_Delete(obj);
  }
  closedir(d);
  return buf;
}
