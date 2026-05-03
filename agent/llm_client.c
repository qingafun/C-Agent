/*
 * llm_client.c — HTTP+JSON client for LLM API communication.
 *
 * Builds structured JSON requests (model, messages, tools schema), sends via
 * HTTP POST with Bearer authentication, parses responses into tool calls and
 * content. Handles empty arguments, non-200 statuses, and TCP socket lifecycle.
 */
#include "llm_client.h"

#include "config.h"
#include "http.h"
#include "tools/tools.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define LLM_TIMEOUT_SEC 120

void llm_response_free(LLMResponse *r) {
  if (!r) return;
  free(r->content);
  free(r->raw_message);
  for (int i = 0; i < r->n_tool_calls; i++) {
    free(r->tool_calls[i].id);
    free(r->tool_calls[i].name);
    cJSON_Delete(r->tool_calls[i].args);
  }
  free(r->tool_calls);
}

int llm_chat(const MessageList *messages, const char *system_prompt,
             const char *model, LLMResponse *out, char *err, size_t err_cap) {
  cJSON *req = cJSON_CreateObject();
  cJSON_AddStringToObject(req, "model", model);
  cJSON_AddNumberToObject(req, "max_tokens", g_config.max_tokens);

  cJSON *msgs = cJSON_AddArrayToObject(req, "messages");
  
  if (system_prompt) {
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", system_prompt);
    cJSON_AddItemToArray(msgs, sys_msg);
  }

  for (int i = 0; i < messages->len; i++) {
    cJSON *msg = cJSON_Parse(messages->items[i]);
    if (msg) cJSON_AddItemToArray(msgs, msg);
  }

  cJSON *tools = cJSON_AddArrayToObject(req, "tools");
  cJSON *tool = cJSON_CreateObject();
  cJSON_AddStringToObject(tool, "type", "function");
  cJSON *func = cJSON_CreateObject();
  cJSON_AddStringToObject(func, "name", BASH_TOOL_NAME);
  cJSON_AddStringToObject(func, "description", BASH_TOOL_DESC);
  cJSON_AddItemToObject(func, "parameters", cJSON_Parse(BASH_TOOL_SCHEMA));
  cJSON_AddItemToObject(tool, "function", func);
  cJSON_AddItemToArray(tools, tool);

  char *req_json = cJSON_PrintUnformatted(req);
  cJSON_Delete(req);

  // TCP
  int fd = tcp_connect(g_config.llm_host, (int)g_config.llm_port, err, err_cap);
  if (fd < 0) {
    free(req_json);
    return -1;
  }

  char *header = xasprintf(
      "POST /api/v1/chat/completions HTTP/1.1\r\n"
      "Host: %s:%d\r\n"
      "Authorization: Bearer %s\r\n"
      "Content-Type: application/json\r\n"
      "Content-Length: %zu\r\n"
      "Connection: close\r\n\r\n",
      g_config.llm_host, (int)g_config.llm_port, g_config.api_key, strlen(req_json));

  if (send_all(fd, header, strlen(header)) < 0 || send_all(fd, req_json, strlen(req_json)) < 0) {
    snprintf(err, err_cap, "Failed to send HTTP request");
    free(header); free(req_json); close(fd);
    return -1;
  }
  free(header);
  free(req_json);

  // Receive
  char *raw_resp = NULL;
  size_t raw_len = 0;
  if (recv_all(fd, LLM_TIMEOUT_SEC, &raw_resp, &raw_len, err, err_cap) < 0) {
    close(fd);
    return -1;
  }
  close(fd);

  int status;
  const char *body;
  if (http_parse_response(raw_resp, &status, &body) < 0 || status != 200) {
    snprintf(err, err_cap, "HTTP Error (status %d) or parse failure.", status);
    free(raw_resp);
    return -1;
  }

  // JSON
  cJSON *resp_json = cJSON_Parse(body);
  if (!resp_json) {
    snprintf(err, err_cap, "Failed to parse JSON response body");
    free(raw_resp);
    return -1;
  }

  cJSON *choices = cJSON_GetObjectItem(resp_json, "choices");
  cJSON *choice = cJSON_GetArrayItem(choices, 0);
  cJSON *message = cJSON_GetObjectItem(choice, "message");

  // Save raw assistant message
  out->raw_message = cJSON_PrintUnformatted(message);

  cJSON *content = cJSON_GetObjectItem(message, "content");
  if (content && cJSON_IsString(content) && content->valuestring != NULL) {
    out->content = xstrdup(content->valuestring);
  } else {
    out->content = xstrdup("");
  }

  cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
  if (tool_calls && cJSON_IsArray(tool_calls)) {
    out->n_tool_calls = cJSON_GetArraySize(tool_calls);
    out->tool_calls = calloc(out->n_tool_calls, sizeof(LLMToolCall));
    
    for (int i = 0; i < out->n_tool_calls; i++) {
      cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
      out->tool_calls[i].id = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(tc, "id")));
      
      cJSON *func_obj = cJSON_GetObjectItem(tc, "function");
      out->tool_calls[i].name = xstrdup(cJSON_GetStringValue(cJSON_GetObjectItem(func_obj, "name")));
      
      const char *args_str = cJSON_GetStringValue(cJSON_GetObjectItem(func_obj, "arguments"));
      if (args_str && strlen(args_str) > 0) {
        out->tool_calls[i].args = cJSON_Parse(args_str);
      } else {
        out->tool_calls[i].args = cJSON_CreateObject(); // Empty arguments
      }
    }
  } else {
    out->n_tool_calls = 0;
    out->tool_calls = NULL;
  }

  cJSON_Delete(resp_json);
  free(raw_resp);
  return 0;
}