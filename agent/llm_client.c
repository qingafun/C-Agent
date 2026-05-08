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
#include "https.h"
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

  int tool_count = 0;
  ToolDef *const *registered_tools = tool_list(&tool_count);

  for (int i = 0; i < tool_count; i++) {
    cJSON *tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
      
    cJSON *func = cJSON_CreateObject();
    cJSON_AddStringToObject(func, "name", registered_tools[i]->name);
    cJSON_AddStringToObject(func, "description", registered_tools[i]->desc);
    cJSON_AddItemToObject(func, "parameters", cJSON_Parse(registered_tools[i]->param_schema));
      
    cJSON_AddItemToObject(tool, "function", func);
    cJSON_AddItemToArray(tools, tool);
  }

  char *req_json = cJSON_PrintUnformatted(req);
  cJSON_Delete(req);

  char *raw_resp = NULL;

  if (g_config.use_https) {
    const char *endpoint = "/api/v1/chat/completions"; 
    char *full_request = xasprintf(
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        g_config.llm_path, g_config.llm_host, g_config.api_key, strlen(req_json), req_json);

    if (https_post_request(g_config.llm_host, g_config.llm_port, full_request, &raw_resp) < 0) {
      snprintf(err, err_cap, "HTTPS request failed (Host: %s)", g_config.llm_host);
      free(full_request);
      free(req_json);
      return -1;
    }
    free(full_request);
  }
  else {
    int fd = tcp_connect(g_config.llm_host, (int)g_config.llm_port, err, err_cap);
    if (fd < 0) {
      free(req_json);
      return -1;
    }

    char *header = xasprintf(
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        g_config.llm_path, g_config.llm_host, (int)g_config.llm_port, g_config.api_key, strlen(req_json));

    if (send_all(fd, header, strlen(header)) != 0 || send_all(fd, req_json, strlen(req_json)) != 0) {
      snprintf(err, err_cap, "Failed to send HTTP data");
      free(header); close(fd); free(req_json);
      return -1;
    }

    size_t raw_len = 0;
    if (recv_all(fd, LLM_TIMEOUT_SEC, &raw_resp, &raw_len, err, err_cap) != 0) {
      free(header); close(fd); free(req_json);
      return -1;
    }
    free(header);
    close(fd);
  }

  free(req_json);

  int status;
  const char *body;
  if (http_parse_response(raw_resp, &status, &body) < 0 || status != 200) {
    fprintf(stderr, "\n[DEBUG] Final server returned status %d. Body:\n%s\n", status, body ? body : "Empty");
    snprintf(err, err_cap, "HTTP Error (status %d) or parse failure.", status);
    free(raw_resp);
    return -1;
  }

  // JSON
  char *json_to_parse = (char *)body;

  // Check if it is chunk transfer
  if (body[0] != '{' && body[0] != '[') {
      char *first_newline = strstr(body, "\r\n");
      if (first_newline) {
          json_to_parse = first_newline + 2; // Real Json
          
          char *end_chunk = strstr(json_to_parse, "\r\n0\r\n\r\n");
          if (end_chunk) {
              *end_chunk = '\0';
          }
      }
  }
  
  cJSON *resp_json = cJSON_Parse(json_to_parse);
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