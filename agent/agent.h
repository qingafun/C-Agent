#ifndef AGENT_H
#define AGENT_H

typedef struct Agent Agent;

Agent *agent_create(int context_window);
void agent_free(Agent *a);
struct Context *agent_get_context(Agent *a);

/*
 * Run one "turn": send user_input to the LLM, execute any tool it asks for,
 * and return the assistant's final text reply. Returned pointer is owned by
 * the agent and is invalidated by the next agent_chat call. Returns NULL on
 * error (and writes a human-readable message via stderr).
 */
const char *agent_chat(Agent *a, const char *user_input);

#endif
