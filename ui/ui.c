/* Terminal UI: static text helpers and event-dispatch to the render thread. */
#include "ui/internal.h"

#include "config.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool g_color = false;
UIState g_ui;

/* ── Static text (main thread only) ───────────────── */

void ui_init(void) {
  g_color = fs_isatty(STDOUT_FILENO) != 0;
  if (g_color)
    terminal_enable_ansi();
}

void ui_banner(void) {
  printf("C Agent - %s\n", g_config.workdir);
  printf("Model: ");
  esc(ESC_GREEN);
  printf("%s", g_config.model);
  esc(ESC_RESET);
  printf("\nType 'exit' to quit.\n");
}

void ui_prompt(void) {
  esc(ESC_BOLD);
  esc(ESC_CYAN);
  printf("> ");
  esc(ESC_RESET);
  fflush(stdout);
}

void ui_error(const char *msg) {
  esc(ESC_RED);
  printf("  Error: %s", msg);
  esc(ESC_RESET);
  printf("\n");
  fflush(stdout);
}

/* ── Slot ownership helpers ───────────────────────── */

static void slot_clear(ToolSlot *s) {
  free(s->name);
  free(s->args_display);
  free(s->result);
  memset(s, 0, sizeof(*s));
}

static void clear_all_slots(void) {
  for (int i = 0; i < g_ui.count; i++)
    slot_clear(&g_ui.slots[i]);
  g_ui.count = 0;
}

/* ── Event queue (caller must hold mutex) ─────────── */

static void push_event_locked(UIEventType type, int slot) {
  int next = (g_ui.tail + 1) % EVENT_QUEUE_SIZE;
  if (next == g_ui.head)
    g_ui.head = (g_ui.head + 1) % EVENT_QUEUE_SIZE;
  g_ui.events[g_ui.tail] = (UIEvent){.type = type, .slot = slot};
  g_ui.tail = next;
}

static void post_event(UIEventType type, int slot) {
  if (!g_ui.active)
    return;
  mutex_lock(&g_ui.mutex);
  if (type == UI_EV_THINKING || type == UI_EV_TOOLS_START)
    g_ui.idle_ack = false;
  push_event_locked(type, slot);
  cond_signal(&g_ui.cond);
  mutex_unlock(&g_ui.mutex);
}

/* ── Lifecycle ────────────────────────────────────── */

void ui_start(void) {
  memset(&g_ui, 0, sizeof(g_ui));
  mutex_init(&g_ui.mutex);
  cond_init(&g_ui.cond);
  if (g_color) {
    g_ui.active = true;
    thread_create(&g_ui.render_tid, render_thread, NULL);
  }
}

void ui_stop(void) {
  if (g_ui.active) {
    post_event(UI_EV_SHUTDOWN, -1);
    thread_join(g_ui.render_tid);
  }
  clear_all_slots();
  mutex_destroy(&g_ui.mutex);
  cond_destroy(&g_ui.cond);
}

/* ── Public events ────────────────────────────────── */

void ui_begin_thinking(void) { post_event(UI_EV_THINKING, -1); }

void ui_begin_tools(int count, const ToolCallView *calls) {
  if (!g_ui.active) {
    /* Without a render thread we just ignore; caller still gets results. */
    return;
  }
  if (count > MAX_RENDER_SLOTS)
    count = MAX_RENDER_SLOTS;

  mutex_lock(&g_ui.mutex);
  clear_all_slots();
  g_ui.count = count;
  for (int i = 0; i < count; i++) {
    g_ui.slots[i].name = xstrdup(calls[i].name ? calls[i].name : "(unknown)");
    g_ui.slots[i].args_display =
        calls[i].args_display ? xstrdup(calls[i].args_display) : NULL;
    g_ui.slots[i].state = TOOL_RUNNING;
    clock_monotonic(&g_ui.slots[i].start_time);
  }
  g_ui.idle_ack = false;
  push_event_locked(UI_EV_TOOLS_START, -1);
  cond_signal(&g_ui.cond);
  mutex_unlock(&g_ui.mutex);
}

void ui_tool_done(int index, bool ok, const char *result) {
  if (!g_ui.active || index < 0)
    return;

  mutex_lock(&g_ui.mutex);
  if (index < g_ui.count) {
    free(g_ui.slots[index].result);
    g_ui.slots[index].result = result ? xstrdup(result) : NULL;
    g_ui.slots[index].state = ok ? TOOL_DONE : TOOL_FAILED;
    clock_monotonic(&g_ui.slots[index].end_time);
    push_event_locked(UI_EV_TOOL_DONE, index);
    cond_signal(&g_ui.cond);
  }
  mutex_unlock(&g_ui.mutex);
}

void ui_idle(void) {
  if (!g_ui.active)
    return;
  post_event(UI_EV_IDLE, -1);
  mutex_lock(&g_ui.mutex);
  while (!g_ui.idle_ack)
    cond_wait(&g_ui.cond, &g_ui.mutex);
  mutex_unlock(&g_ui.mutex);
}
