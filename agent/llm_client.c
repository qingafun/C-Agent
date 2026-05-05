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

  char *raw_resp = NULL;
  int fallback_to_https = 1;

  // Try HTTP first
  int fd = tcp_connect(g_config.llm_host, (int)g_config.llm_port, err, err_cap);
  if (fd >= 0) {
    char *header = xasprintf(
        "POST /api/v1/chat/completions HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        g_config.llm_host, (int)g_config.llm_port, g_config.api_key, strlen(req_json));

    if (send_all(fd, header, strlen(header)) == 0 && send_all(fd, req_json, strlen(req_json)) == 0) {
      size_t raw_len = 0;
      if (recv_all(fd, LLM_TIMEOUT_SEC, &raw_resp, &raw_len, err, err_cap) == 0) {
        int status;
        const char *temp_body;
        if (http_parse_response(raw_resp, &status, &temp_body) == 0 && status == 200) {
          fallback_to_https = 0;
        } else {
          fprintf(stderr, "\n[INFO] HTTP attempt returned status %d. Triggering HTTPS fallback...\n", status);
        }
      }
    }
    free(header);
    close(fd);
  } else {
    fprintf(stderr, "\n[INFO] HTTP connection to %s:%d failed. Triggering HTTPS fallback...\n", 
            g_config.llm_host, (int)g_config.llm_port);
  }

  // Try HTTPS if HTTP failed
  if (fallback_to_https) {
    if (raw_resp) { free(raw_resp); raw_resp = NULL; }

    const char *fallback_host = "api.deepseek.com"; 
    int fallback_port = 443;
    const char *endpoint = "/chat/completions"; 

    char *full_request = xasprintf(
        "POST %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n"
        "%s",
        endpoint, fallback_host, g_config.api_key, strlen(req_json), req_json);

    if (https_post_request(fallback_host, fallback_port, full_request, &raw_resp) < 0) {
      snprintf(err, err_cap, "Both HTTP and HTTPS fallback completely failed.");
      free(full_request);
      free(req_json);
      return -1;
    }
    free(full_request);
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