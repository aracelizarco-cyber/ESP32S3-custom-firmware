#pragma once
#ifdef __cplusplus
extern "C" {
#endif

void ota_init(void);
// Set default URL (persisted)
void ota_set_url(const char* url);
void ota_get_url(char* buf, int buf_sz);
// Start OTA in a background task (uses stored URL if url==NULL or empty)
void ota_trigger(const char* url_opt);

#ifdef __cplusplus
}
#endif