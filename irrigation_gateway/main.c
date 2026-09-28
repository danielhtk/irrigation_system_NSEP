#include "global.h"
#include "http_client.h"
#include "gateway_threads.h"

static struct termios orig_termios;

void reset_terminal_mode(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
	}

void set_conio_terminal_mode(void) {
    struct termios new_termios;

    /* Save current settings, restore on exit. */
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(reset_terminal_mode);

    /* Raw mode: no canonical input, no echo. */
    new_termios = orig_termios;
    new_termios.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);
	}

int kbhit(void) {
    struct timeval tv = {0L, 0L};
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    return select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
	}

int getch(void) {
    unsigned char c;
    if (read(STDIN_FILENO, &c, sizeof(c)) < 0) return -1;
    return c;
	}

void on_device_found(const char *name, const char *mac, const char* path) {
	if(strncmp(name, BLE_NAME, sizeof(BLE_NAME)) == 0) {
	    printf("Found device %s (%s), path=%s\n", name, mac, path);
	    strncpy(BLE_PATH, path, sizeof(BLE_PATH));
	    BLE_FOUND = 1;
	    }
	}

int main() {
    uint64_t timeout;
    uint8_t running = 1;
    uint8_t ch;

    SYS_Error_Check(SYS_Init());
    SYS_Error_Check(http_client_init());
    SYS_Error_Check(gateway_threads_start());

    SYS_Error_Check(BLE_Scan_Start());
    printf("Scanning for %s for %u seconds\n", BLE_NAME, BLE_SCAN_DURATION);

    timeout = SYS_TICK + (BLE_SCAN_DURATION * 1000);
    while((SYS_TICK < timeout) && (!BLE_FOUND)) {
        BLE_Poll(on_device_found);
        usleep(100000);
    }
    
    if(!BLE_FOUND) {
        printf("Did not find device %s after %u seconds\n", BLE_NAME, BLE_SCAN_DURATION);
        return 1;
    }

    printf("Connecting device\n");   
    SYS_Error_Check(BLE_Connect(BLE_PATH));
    printf("Device connected\n");
    
    printf("Resolving service and characteristic\n");
    SYS_Error_Check(BLE_Wait_For_Services_Resolved(BLE_PATH, 5000));
    printf("Service and characteristic resolved\n");
    
    printf("Enabling notification\n");
    SYS_Error_Check(BLE_Notification_Enable(BLE_SERVICE_UUID, BLE_CHARACTERISTIC_UUID, BLE_Receive));
    printf("Notification enabled\n");
    printf("Receive loop... press ESC to exit\n");
    set_conio_terminal_mode();
    
    int32_t err_code;
    /* Socket lines are <256B; 4KB is plenty. */
    char str1[4096];
    int32_t rx_bytes;

    while(running) {
        dbus_connection_read_write_dispatch(BLE_CONN, 100);

        if(kbhit()) {
            ch = getch();
            /* ESC quits. */
            if(ch == 27) {
                printf("ESC pressed, disconnecting...\n");
                running = 0;
            }
        }
        if((err_code = CLIENT_Read((uint8_t*)str1, sizeof(str1) - 1, &rx_bytes)) ==  CLIENT_SUCCESS) {
            if(rx_bytes > 0 && rx_bytes < (int32_t)sizeof(str1)) {
                str1[rx_bytes] = '\0';
                printf("%s", str1);
            }
        }
    }
    
    gateway_threads_stop();
    http_client_cleanup();
    BLE_Disconnect();
    reset_terminal_mode();
    return 0;
}

