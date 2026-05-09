#include "agent/agent.h"
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
  signal(SIGINT, sigint_handler);
  config_init();
  https_init();
  tools_init();

  Agent *a = agent_create();

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
  agent_free(a);
  https_cleanup();

  return rc;
}