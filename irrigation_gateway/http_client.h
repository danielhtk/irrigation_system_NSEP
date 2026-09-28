#ifndef __HTTP_CLIENT_H__
#define __HTTP_CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define HTTP_API_BASE "http://localhost"  // Change for production

typedef struct {
    char *data;
    size_t size;
} curl_buffer_t;

int32_t http_client_init(void);
void http_client_cleanup(void);

/* Fetch rain forecast from Open-Meteo
 * Returns 0 on success, sets *out_mm to total precipitation (int mm, truncated)
 */
int32_t http_fetch_rain_forecast(float lat, float lon, int hours, int *out_mm);

/* POST a command to the API command queue */
int32_t http_post_command(const char *cmd);

/* Fetch next pending command from API
 * Returns: 0 = command fetched (copied to cmd_buf), 1 = no pending command, -1 = error
 * If cmd_id_out is not NULL, stores the command ID there
 */
int32_t http_fetch_next_command(char *cmd_buf, size_t buf_size, int *cmd_id_out);

/* Acknowledge command result */
int32_t http_ack_command(int cmd_id, bool success);

/* Initialize HTTP client (call once at startup) */
int32_t http_client_init(void);

/* Cleanup (call at shutdown) */
void http_client_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* __HTTP_CLIENT_H__ */