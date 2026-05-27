#include "agent/agent.h"
#include "compat.h"
#include "config.h"
#include "https.h"
#include "ui/ui.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tools/tools.h"

#define INPUT_BUF 4096

static volatile sig_atomic_t g_running = 1;

static void sigint_handler(int sig) {
  (void)sig;
  g_running = 0;
}

int main(void) {
  if (net_init() != 0) {
    fprintf(stderr, "Fatal: network init failed\n");
    return 1;
  }
  signal(SIGINT, sigint_handler);
  config_init();
  https_init();
  tools_init();
  session_log_init();

  Agent *a = agent_create(g_config.context_window);
  session_set_context(agent_get_context(a));

  if (session_has_pending()) {
    printf("Found previous session. Resume? [Y/n]: ");
    fflush(stdout);
    char answer[16];
    if (fgets(answer, sizeof(answer), stdin)) {
      char c = answer[0];
      if (c == 'n' || c == 'N') {
        printf("Starting a new session.\n");
        char path[PATH_MAX + 256];
        snprintf(path, sizeof(path), "%s/.agent/sessions/current.jsonl",
                 g_config.workdir);
        remove(path);
      } else {
        session_replay();
      }
    }
  }

  ui_init();
  ui_start();
  ui_banner();

  printf("Terminal Coding Agent Started. Type 'exit', 'quit', or 'q' to quit.\n");

  char input[INPUT_BUF];
  int rc = 0;

  while (g_running) {
    printf("> ");
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) {
      break;
    }

    if (!g_running)
      break;

    size_t len = strlen(input);
    if (len > 0 && input[len - 1] == '\n') {
      input[len - 1] = '\0';
    }

    if (strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0 || strcmp(input, "q") == 0) {
      break;
    }

    if (strlen(input) == 0) {
      continue;
    }

    const char *reply = agent_chat(a, input);

    if (reply) {
      printf("%s\n", reply);
    }
    else {
      rc = 1;
      break;
    }
  }

  ui_stop();
  session_log_close();
  agent_free(a);
  https_cleanup();
  net_cleanup();

  return rc;
}
