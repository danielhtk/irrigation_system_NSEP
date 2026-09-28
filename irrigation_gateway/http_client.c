/**
 * http_client.c - HTTP client for gateway using libcurl
 * Fetches Open-Meteo forecast and polls command queue
 */

#include "http_client.h"
#include "global.h"
#include <curl/curl.h>
#include <cjson/cJSON.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static CURL *curl_handle = NULL;
static char curl_error[CURL_ERROR_SIZE];

/* Callback: append HTTP chunk to buffer. */
static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    curl_buffer_t *buf = (curl_buffer_t *)userp;
    char *ptr = realloc(buf->data, buf->size + realsize + 1);
    if (!ptr) return 0;
    buf->data = ptr;
    memcpy(&(buf->data[buf->size]), contents, realsize);
    buf->size += realsize;
    buf->data[buf->size] = '\0';
    return realsize;
}

int32_t http_client_init(void) {
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();
    if (!curl_handle) return -1;
    curl_easy_setopt(curl_handle, CURLOPT_ERRORBUFFER, curl_error);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 10L);
    curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
    /* Dev only: self-signed certs. Enable verify in production. */
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);
    return 0;
}

void http_client_cleanup(void) {
    if (curl_handle) {
        curl_easy_cleanup(curl_handle);
        curl_handle = NULL;
    }
    curl_global_cleanup();
}

/* Rain forecast for next N hours (Open-Meteo). */
int32_t http_fetch_rain_forecast(float lat, float lon, int hours, int *out_mm) {
    if (!curl_handle) return -1;
    if (!out_mm) return -1;

    char url[512];
    snprintf(url, sizeof(url),
        "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&hourly=precipitation&forecast_hours=%d&timezone=UTC",
        lat, lon, hours);

    curl_buffer_t buf = { .data = NULL, .size = 0 };
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buf);

    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        fprintf(stderr, "curl error: %s\n", curl_error);
        free(buf.data);
        return -1;
    }

    cJSON *json = cJSON_Parse(buf.data);
    free(buf.data);
    if (!json) return -1;

    cJSON *hourly = cJSON_GetObjectItem(json, "hourly");
    if (!hourly) { cJSON_Delete(json); return -1; }

    cJSON *precip = cJSON_GetObjectItem(hourly, "precipitation");
    if (!cJSON_IsArray(precip)) { cJSON_Delete(json); return -1; }

    double total = 0.0;
    int count = cJSON_GetArraySize(precip);
    int limit = (count < hours) ? count : hours;
    for (int i = 0; i < limit; i++) {
        cJSON *item = cJSON_GetArrayItem(precip, i);
        if (cJSON_IsNumber(item)) total += item->valuedouble;
    }
    cJSON_Delete(json);

    *out_mm = (int)total;  // truncate to int (floor for non-negative)
    return 0;
}

/* POST command to API */
int32_t http_post_command(const char *cmd) {
    if (!curl_handle || !cmd) return -1;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/command", HTTP_API_BASE);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "cmd", cmd);
    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_buffer_t buf = { .data = NULL, .size = 0 };
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);

    CURLcode res = curl_easy_perform(curl_handle);
    free(json_str);
    curl_slist_free_all(headers);
    free(buf.data);

    if (res != CURLE_OK) {
        fprintf(stderr, "POST command failed: %s\n", curl_error);
        return -1;
    }
    return 0;
}

/* Fetch next pending command from API
 * Returns: 0 = command fetched (copied to cmd_buf), 1 = no pending command, -1 = error
 * If cmd_id_out is not NULL, stores the command ID there
 */
int32_t http_fetch_next_command(char *cmd_buf, size_t buf_size, int *cmd_id_out) {
    if (!curl_handle || !cmd_buf || buf_size == 0) return -1;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/command/next", HTTP_API_BASE);

    curl_buffer_t buf = { .data = NULL, .size = 0 };
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &buf);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, write_callback);

    CURLcode res = curl_easy_perform(curl_handle);
    if (res != CURLE_OK) {
        free(buf.data);
        return -1;
    }

    cJSON *json = cJSON_Parse(buf.data);
    free(buf.data);
    if (!json) return -1;

    cJSON *ok = cJSON_GetObjectItem(json, "ok");
    if (!cJSON_IsTrue(ok)) { cJSON_Delete(json); return -1; }

    cJSON *cmd = cJSON_GetObjectItem(json, "cmd");
    if (cmd && cJSON_IsString(cmd) && cmd->valuestring) {
        strncpy(cmd_buf, cmd->valuestring, buf_size - 1);
        cmd_buf[buf_size - 1] = '\0';
        if (cmd_id_out) {
            cJSON *id = cJSON_GetObjectItem(json, "id");
            *cmd_id_out = cJSON_IsNumber(id) ? id->valueint : 0;
        }
        cJSON_Delete(json);
        return 0;
    }

    cJSON *cmd_null = cJSON_GetObjectItem(json, "cmd");
    if (cmd_null && cJSON_IsNull(cmd_null)) {
        cJSON_Delete(json);
        return 1;  // No pending command
    }

    cJSON_Delete(json);
    return -1;
}

/* Acknowledge command */
int32_t http_ack_command(int cmd_id, bool success) {
    if (!curl_handle) return -1;

    char url[256];
    snprintf(url, sizeof(url), "%s/api/command/ack", HTTP_API_BASE);

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "id", cmd_id);
    cJSON_AddStringToObject(json, "status", success ? "acked" : "failed");
    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_POST, 1L);
    curl_easy_setopt(curl_handle, CURLOPT_POSTFIELDS, json_str);
    curl_easy_setopt(curl_handle, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, NULL);

    CURLcode res = curl_easy_perform(curl_handle);
    free(json_str);
    curl_slist_free_all(headers);

    return (res == CURLE_OK) ? 0 : -1;
}