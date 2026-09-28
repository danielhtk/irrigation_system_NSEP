/**
 * gateway_threads.c - Background threads for gateway
 * - Forecast fetcher (hourly)
 * - Command poller (5s)
 * - Config poller (30s)
 */

#include "gateway_threads.h"
#include "http_client.h"
#include "ble.h"
#include "global.h"
#include <pthread.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#define FORECAST_LAT 2.92f
#define FORECAST_LON 101.66f
#define FORECAST_HOURS 12

static pthread_t forecast_thread;
static pthread_t command_thread;
static pthread_t config_thread;
static volatile bool threads_running = false;

static void *forecast_worker(void *arg) {
    printf("[forecast] Thread started\n");
    while (threads_running) {
        int mm = 0;
        int rc = http_fetch_rain_forecast(FORECAST_LAT, FORECAST_LON, FORECAST_HOURS, &mm);
        if (rc == 0 && mm >= 2) {  /* RAIN_FORECAST_Y_MM */
            char cmd[64];
            snprintf(cmd, sizeof(cmd), "RAINF:%d,%d", mm, FORECAST_HOURS);
            printf("[forecast] Sending %s\n", cmd);
            http_post_command(cmd);
        } else if (rc != 0) {
            fprintf(stderr, "[forecast] Fetch failed\n");
        }
        /* Sleep 1 hour. */
        for (int i = 0; i < 3600 && threads_running; i++) sleep(1);
    }
    return NULL;
}

static void *command_worker(void *arg) {
    printf("[command] Thread started\n");
    while (threads_running) {
        char cmd[128];
        int cmd_id = 0;
        int rc = http_fetch_next_command(cmd, sizeof(cmd), &cmd_id);
        if (rc == 0) {
            printf("[command] Got command: %s (id=%d)\n", cmd, cmd_id);
            /* Arduino parser needs trailing newline. */
            char cmd_with_nl[132];
            snprintf(cmd_with_nl, sizeof(cmd_with_nl), "%s\n", cmd);
            int ble_rc = BLE_Transmit(BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID,
                                      (const uint8_t *)cmd_with_nl, strlen(cmd_with_nl));
            if (ble_rc == 0) {
                http_ack_command(cmd_id, true);
            } else {
                fprintf(stderr, "[command] BLE transmit failed\n");
                http_ack_command(cmd_id, false);
            }
        } else if (rc < 0) {
            fprintf(stderr, "[command] Fetch failed\n");
        }
        /* Poll every 5 seconds. */
        for (int i = 0; i < 5 && threads_running; i++) sleep(1);
    }
    return NULL;
}

static void *config_worker(void *arg) {
    printf("[config] Thread started\n");
    while (threads_running) {
        /* Threshold sync not yet implemented; poll /api/config here. */
        for (int i = 0; i < 30 && threads_running; i++) sleep(1);
    }
    return NULL;
}

int32_t gateway_threads_start(void) {
    if (threads_running) return 0;
    threads_running = true;

    int rc = pthread_create(&forecast_thread, NULL, forecast_worker, NULL);
    if (rc != 0) { fprintf(stderr, "Failed to create forecast thread\n"); return -1; }

    rc = pthread_create(&command_thread, NULL, command_worker, NULL);
    if (rc != 0) { fprintf(stderr, "Failed to create command thread\n"); return -1; }

    rc = pthread_create(&config_thread, NULL, config_worker, NULL);
    if (rc != 0) { fprintf(stderr, "Failed to create config thread\n"); return -1; }

    printf("[gateway] Background threads started\n");
    return 0;
}

void gateway_threads_stop(void) {
    if (!threads_running) return;
    threads_running = false;
    pthread_join(forecast_thread, NULL);
    pthread_join(command_thread, NULL);
    pthread_join(config_thread, NULL);
    printf("[gateway] Background threads stopped\n");
}