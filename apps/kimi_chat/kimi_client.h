#ifndef KIMI_CLIENT_H
#define KIMI_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	const char *role;
	const char *content;
} kimi_client_message_t;

typedef struct {
	int http_status;
	char *reply;
	char *response_id;
	char *error_message;
	char *response_json;
} kimi_client_result_t;

int kimi_client_chat(const char *api_key,
		const char *model,
		const char *thinking_mode,
		const kimi_client_message_t *messages,
		int message_count,
		int timeout_ms,
		kimi_client_result_t *out);

void kimi_client_result_clear(kimi_client_result_t *out);

#ifdef __cplusplus
}
#endif

#endif
