#include "kimi_client.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "tinyhttpsc/tinyhttpsc.h"

#ifndef BEARSSL_HTTPS_GET_OWNERSHIP
#define BEARSSL_HTTPS_GET_OWNERSHIP 1
#endif

typedef struct cJSON cJSON;
typedef int cJSON_bool;

extern cJSON *cJSON_Parse(const char *value);
extern char *cJSON_Print(const cJSON *item);
extern void cJSON_Delete(cJSON *item);
extern void cJSON_free(void *object);
extern int cJSON_GetArraySize(const cJSON *array);
extern cJSON *cJSON_GetArrayItem(const cJSON *array, int index);
extern cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON * const object, const char * const string);
extern char *cJSON_GetStringValue(const cJSON * const item);
extern cJSON_bool cJSON_IsString(const cJSON * const item);
extern cJSON_bool cJSON_IsArray(const cJSON * const item);
extern cJSON_bool cJSON_IsObject(const cJSON * const item);
extern cJSON *cJSON_CreateObject(void);
extern cJSON *cJSON_AddArrayToObject(cJSON * const object, const char * const name);
extern cJSON *cJSON_AddStringToObject(cJSON * const object, const char * const name, const char * const string);
extern cJSON_bool cJSON_AddItemToArray(cJSON *array, cJSON *item);
extern cJSON *cJSON_AddObjectToObject(cJSON * const object, const char * const name);

#define KIMI_CLIENT_DEFAULT_TIMEOUT_MS 15000

static char *kimi_client_strdup(const char *value) {
	size_t size;
	char *copy;

	if (value == NULL) {
		return NULL;
	}

	size = strlen(value) + 1;
	copy = (char *)malloc(size);
	if (copy == NULL) {
		return NULL;
	}

	memcpy(copy, value, size);
	return copy;
}

static char *kimi_client_strdup_printf(const char *fmt, ...) {
	va_list args;
	va_list args_copy;
	int needed;
	char *buffer;

	va_start(args, fmt);
	va_copy(args_copy, args);
	needed = vsnprintf(NULL, 0, fmt, args_copy);
	va_end(args_copy);
	if (needed < 0) {
		va_end(args);
		return NULL;
	}

	buffer = (char *)malloc((size_t)needed + 1);
	if (buffer == NULL) {
		va_end(args);
		return NULL;
	}

	vsnprintf(buffer, (size_t)needed + 1, fmt, args);
	va_end(args);
	return buffer;
}

static void kimi_client_append_text(char **buffer, size_t *size, const char *text) {
	size_t old_size;
	size_t add_size;
	char *next;

	if (buffer == NULL || size == NULL || text == NULL || text[0] == '\0') {
		return;
	}

	old_size = *size;
	add_size = strlen(text);
	next = (char *)realloc(*buffer, old_size + add_size + 1);
	if (next == NULL) {
		return;
	}

	memcpy(next + old_size, text, add_size);
	next[old_size + add_size] = '\0';
	*buffer = next;
	*size = old_size + add_size;
}

static const char *kimi_client_get_string(const cJSON *item) {
	if (!cJSON_IsString(item)) {
		return NULL;
	}
	return cJSON_GetStringValue(item);
}

static char *kimi_client_json_to_string(const cJSON *json) {
	char *printed;
	char *copy;

	if (json == NULL) {
		return NULL;
	}

	printed = cJSON_Print(json);
	if (printed == NULL) {
		return NULL;
	}

	copy = kimi_client_strdup(printed);
	cJSON_free(printed);
	return copy;
}

static char *kimi_client_extract_message_content(const cJSON *message) {
	const cJSON *content;
	const cJSON *reasoning;
	const char *string_value;
	char *joined = NULL;
	size_t joined_size = 0;
	int total;
	int i;

	if (message == NULL) {
		return NULL;
	}

	content = cJSON_GetObjectItemCaseSensitive(message, "content");
	string_value = kimi_client_get_string(content);
	if (string_value != NULL && string_value[0] != '\0') {
		return kimi_client_strdup(string_value);
	}

	if (!cJSON_IsArray(content)) {
		reasoning = cJSON_GetObjectItemCaseSensitive(message, "reasoning_content");
		string_value = kimi_client_get_string(reasoning);
		if (string_value != NULL && string_value[0] != '\0') {
			return kimi_client_strdup(string_value);
		}
		return NULL;
	}

	total = cJSON_GetArraySize(content);
	for (i = 0; i < total; ++i) {
		const cJSON *part = cJSON_GetArrayItem(content, i);
		const cJSON *type;
		const cJSON *text;
		const char *type_value;
		const char *text_value;

		string_value = kimi_client_get_string(part);
		if (string_value != NULL) {
			kimi_client_append_text(&joined, &joined_size, string_value);
			continue;
		}

		if (!cJSON_IsObject(part)) {
			continue;
		}

		type = cJSON_GetObjectItemCaseSensitive(part, "type");
		text = cJSON_GetObjectItemCaseSensitive(part, "text");
		type_value = kimi_client_get_string(type);
		text_value = kimi_client_get_string(text);
		if (text_value != NULL && (type_value == NULL || strcmp(type_value, "text") == 0)) {
			kimi_client_append_text(&joined, &joined_size, text_value);
		}
	}

	if (joined == NULL || joined[0] == '\0') {
		reasoning = cJSON_GetObjectItemCaseSensitive(message, "reasoning_content");
		string_value = kimi_client_get_string(reasoning);
		if (string_value != NULL && string_value[0] != '\0') {
			free(joined);
			return kimi_client_strdup(string_value);
		}
	}

	return joined;
}

static char *kimi_client_extract_reply(const cJSON *root) {
	const cJSON *choices;
	const cJSON *choice;
	const cJSON *message;

	if (root == NULL) {
		return NULL;
	}

	choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
	if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) <= 0) {
		return NULL;
	}

	choice = cJSON_GetArrayItem(choices, 0);
	if (!cJSON_IsObject(choice)) {
		return NULL;
	}

	message = cJSON_GetObjectItemCaseSensitive(choice, "message");
	if (!cJSON_IsObject(message)) {
		return NULL;
	}

	return kimi_client_extract_message_content(message);
}

static char *kimi_client_extract_error_message(const cJSON *root) {
	const cJSON *error;
	const cJSON *message;
	const char *string_value;

	if (root == NULL) {
		return NULL;
	}

	error = cJSON_GetObjectItemCaseSensitive(root, "error");
	if (cJSON_IsObject(error)) {
		message = cJSON_GetObjectItemCaseSensitive(error, "message");
		string_value = kimi_client_get_string(message);
		if (string_value != NULL) {
			return kimi_client_strdup(string_value);
		}
		return kimi_client_json_to_string(error);
	}

	message = cJSON_GetObjectItemCaseSensitive(root, "message");
	string_value = kimi_client_get_string(message);
	if (string_value != NULL) {
		return kimi_client_strdup(string_value);
	}

	return kimi_client_json_to_string(root);
}

static char *kimi_client_extract_response_id(const cJSON *root) {
	const cJSON *id;
	const char *string_value;

	if (root == NULL) {
		return NULL;
	}

	id = cJSON_GetObjectItemCaseSensitive(root, "id");
	string_value = kimi_client_get_string(id);
	if (string_value != NULL) {
		return kimi_client_strdup(string_value);
	}

	return NULL;
}

void kimi_client_result_clear(kimi_client_result_t *out) {
	if (out == NULL) {
		return;
	}

	if (out->reply != NULL) {
		free(out->reply);
	}
	if (out->response_id != NULL) {
		free(out->response_id);
	}
	if (out->error_message != NULL) {
		free(out->error_message);
	}
	if (out->response_json != NULL) {
		free(out->response_json);
	}

	memset(out, 0, sizeof(*out));
}

int kimi_client_chat(const char *api_key,
		const char *model,
		const char *thinking_mode,
		const kimi_client_message_t *messages,
		int message_count,
		int timeout_ms,
		kimi_client_result_t *out) {
	TinyHttpsRequest *request = NULL;
	TinyHttpsResponse *response = NULL;
	cJSON *payload = NULL;
	cJSON *messages_json = NULL;
	cJSON *response_json = NULL;
	const char *body_str = NULL;
	const char *model_name = "kimi-k2.5";
	const char *fetch_error;
	int fetch_error_code;
	int i;
	int rc = -1;

	if (out == NULL) {
		return -1;
	}
	memset(out, 0, sizeof(*out));

	if (api_key == NULL || api_key[0] == '\0') {
		out->error_message = kimi_client_strdup("missing Moonshot API key");
		return -1;
	}

	if (messages == NULL || message_count <= 0) {
		out->error_message = kimi_client_strdup("missing chat messages");
		return -1;
	}

	if (model != NULL && model[0] != '\0') {
		model_name = model;
	}

	request = NewHttpsRequest("https://api.moonshot.cn/v1/chat/completions");
	if (request == NULL) {
		out->error_message = kimi_client_strdup("cannot allocate HTTPS request");
		goto cleanup;
	}

	HttpsRequestSetTimeout(request, timeout_ms > 0 ? timeout_ms : KIMI_CLIENT_DEFAULT_TIMEOUT_MS);
	HttpsRequestSetMethod(request, "POST");
	HttpsRequestSetMaxRedirections(request, 0);
	BearHttpsRequest_add_header_fmt(request, "Authorization", "Bearer %s", api_key);
	HttpsRequestAddHeader(request, "Accept", "application/json");
	HttpsRequestAddHeader(request, "Content-Type", "application/json");
	HttpsRequestAddHeader(request, "User-Agent", "ewokos-kimi-chat/1");

	payload = cJSON_CreateObject();
	if (payload == NULL) {
		out->error_message = kimi_client_strdup("cannot allocate JSON payload");
		goto cleanup;
	}

	if (cJSON_AddStringToObject(payload, "model", model_name) == NULL) {
		out->error_message = kimi_client_strdup("cannot encode model");
		goto cleanup;
	}

	messages_json = cJSON_AddArrayToObject(payload, "messages");
	if (messages_json == NULL) {
		out->error_message = kimi_client_strdup("cannot allocate messages array");
		goto cleanup;
	}

	for (i = 0; i < message_count; ++i) {
		cJSON *message_json;
		const char *role = messages[i].role;
		const char *content = messages[i].content;

		if (role == NULL || role[0] == '\0' || content == NULL || content[0] == '\0') {
			continue;
		}

		message_json = cJSON_CreateObject();
		if (message_json == NULL) {
			out->error_message = kimi_client_strdup("cannot allocate message JSON");
			goto cleanup;
		}

		if (cJSON_AddStringToObject(message_json, "role", role) == NULL ||
				cJSON_AddStringToObject(message_json, "content", content) == NULL ||
				!cJSON_AddItemToArray(messages_json, message_json)) {
			cJSON_Delete(message_json);
			out->error_message = kimi_client_strdup("cannot encode message JSON");
			goto cleanup;
		}
	}

	if (thinking_mode != NULL && thinking_mode[0] != '\0' &&
			strcmp(thinking_mode, "default") != 0 &&
			strcmp(thinking_mode, "auto") != 0) {
		cJSON *extra_body = cJSON_AddObjectToObject(payload, "extra_body");
		cJSON *thinking = NULL;

		if (extra_body != NULL) {
			thinking = cJSON_AddObjectToObject(extra_body, "thinking");
		}
		if (thinking == NULL || cJSON_AddStringToObject(thinking, "type", thinking_mode) == NULL) {
			out->error_message = kimi_client_strdup("cannot encode thinking config");
			goto cleanup;
		}
	}

	BearHttpsRequest_send_cJSON_with_ownership_control(request, payload, BEARSSL_HTTPS_GET_OWNERSHIP);
	payload = NULL;

	response = HttpsRequestFetch(request);
	if (response == NULL) {
		out->error_message = kimi_client_strdup("fetch returned null response");
		goto cleanup;
	}

	out->http_status = HttpsResponseGetStatusCode(response);

	if (HttpsResponseError(response)) {
		fetch_error = HttpsResponseGetErrorMsg(response);
		fetch_error_code = HttpsResponseGetErrorCode(response);
		if (fetch_error == NULL || fetch_error[0] == '\0') {
			fetch_error = "HTTPS request failed";
		}
		out->error_message = kimi_client_strdup_printf("%s (code=%d)", fetch_error, fetch_error_code);
		goto cleanup;
	}

	body_str = HttpsResponseReadBodyStr(response, NULL);
	if (body_str != NULL) {
		out->response_json = kimi_client_strdup(body_str);
		response_json = cJSON_Parse(body_str);
	}

	if (response_json != NULL) {
		out->response_id = kimi_client_extract_response_id(response_json);
	}

	if (out->http_status >= 200 && out->http_status < 300) {
		out->reply = kimi_client_extract_reply(response_json);
		if (out->reply == NULL) {
			out->error_message = kimi_client_strdup("response has no assistant message");
			goto cleanup;
		}
		rc = 0;
		goto cleanup;
	}

	out->error_message = kimi_client_extract_error_message(response_json);
	if (out->error_message == NULL && out->response_json != NULL) {
		out->error_message = kimi_client_strdup(out->response_json);
	}
	if (out->error_message == NULL) {
		out->error_message = kimi_client_strdup_printf("HTTP %d", out->http_status);
	}

cleanup:
	if (response_json != NULL) {
		cJSON_Delete(response_json);
	}
	if (payload != NULL) {
		cJSON_Delete(payload);
	}
	if (response != NULL) {
		HttpsResponseFree(response);
	}
	if (request != NULL) {
		HttpsRequestFree(request);
	}
	if (rc != 0 && out->error_message == NULL) {
		out->error_message = kimi_client_strdup("request failed");
	}

	return rc;
}
