#include "config.h"
#include "compat.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

AgentConfig g_config;

static void copy_env_string(char *dst, size_t cap, const char *name,
                            const char *fallback) {
  const char *value = getenv(name);
  snprintf(dst, cap, "%s", (value && value[0]) ? value : fallback);
}

static void strip_scheme(char *host) {
  const char *schemes[] = {"https://", "http://"};
  for (size_t i = 0; i < 2; i++) {
    size_t len = strlen(schemes[i]);
    if (strncmp(host, schemes[i], len) == 0) {
      memmove(host, host + len, strlen(host + len) + 1);
      return;
    }
  }
}

static int parse_env_int(const char *name, int fallback, int min_value,
                         int max_value) {
  const char *value = getenv(name);
  if (!value || !value[0])
    return fallback;
  errno = 0;
  char *end = NULL;
  long parsed = strtol(value, &end, 10);
  if (errno != 0 || end == value || *end != '\0' || parsed < min_value ||
      parsed > max_value) {
    fprintf(stderr, "[config] warning: invalid %s=%s, using %d\n", name, value,
            fallback);
    return fallback;
  }
  return (int)parsed;
}

void config_init(void) {
  copy_env_string(g_config.model, sizeof(g_config.model), "MODEL_ID",
                  "deepseek-chat");
  copy_env_string(g_config.llm_host, sizeof(g_config.llm_host), "LLM_HOST",
                  "api.deepseek.com");
  strip_scheme(g_config.llm_host);
  copy_env_string(g_config.api_key, sizeof(g_config.api_key), "API_KEY",
                  "none");

  g_config.llm_port = parse_env_int("LLM_PORT", 443, 1, 65535);
  g_config.max_tokens = parse_env_int("MAX_TOKENS", 8000, 1, INT_MAX);

  const char *env_path = getenv("LLM_PATH");
  if (env_path && env_path[0]) {
    snprintf(g_config.llm_path, sizeof(g_config.llm_path), "%s", env_path);
  } else {
    if (strcmp(g_config.llm_host, "127.0.0.1") == 0 || g_config.llm_port == 18180) {
      strcpy(g_config.llm_path, "/api/v1/chat/completions");
    } else {
      strcpy(g_config.llm_path, "/v1/chat/completions");
    }
  }

  if (g_config.llm_port == 443 || strstr(g_config.llm_host, "api.")) {
    g_config.use_https = true;
  } else {
    g_config.use_https = false;
  }

  /* Canonicalize so tools and logs see the same path shape. */
  if (!fs_realpath(".", g_config.workdir)) {
    if (!fs_getcwd(g_config.workdir, sizeof(g_config.workdir))) {
      perror("getcwd");
      exit(1);
    }
  }
}
