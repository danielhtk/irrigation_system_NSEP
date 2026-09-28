#include "global.h"

char CLIENT_SERVER_NAME[80];
uint16_t CLIENT_SERVER_PORT;
CLIENT_STATE_e CLIENT_STATE;
int32_t CLIENT_SD;

int wait_for_read(int fd, int timeout_ms) {
    struct pollfd pfd;
    pfd.fd = fd;
    /* Wait for readability. */
    pfd.events = POLLIN;

    return poll(&pfd, 1, timeout_ms);
	}

int32_t CLIENT_Init(void) {
	CLIENT_STATE = CLIENT_STATE_CLOSED;
	
	CLIENT_SERVER_PORT = CLIENT_SERVER_PORT_DEFAULT;
	strcpy(CLIENT_SERVER_NAME, CLIENT_SERVER_NAME_DEFAULT);
	
	if(CLIENT_Connect() == -1) return CLIENT_ERROR_INIT;
	else return CLIENT_SUCCESS;
	}

/* Connect to host. Returns -1 on error. */
int32_t CLIENT_Connect(void) {
	struct hostent *sp;
	int socket_mode = 1;
	struct sockaddr_in server;
	
	CLIENT_STATE = CLIENT_STATE_CLOSED;
	
	CLIENT_SD = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if(CLIENT_SD == -1) return CLIENT_ERROR_OPEN_SOCKET;

	memset((char *) &server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons((u_short) CLIENT_SERVER_PORT);
	sp = gethostbyname(CLIENT_SERVER_NAME);
	
	if(sp) memcpy(&server.sin_addr, sp->h_addr, sp->h_length);	
	else return CLIENT_ERROR_MEMORY_ALLOCATION;

	if(connect(CLIENT_SD, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1) {
		printf("CLI: connect failed: %s\n", strerror(errno));
		return CLIENT_ERROR_CONNECT;
		}
	if(ioctl(CLIENT_SD, FIONBIO, &socket_mode) == -1) return CLIENT_ERROR_IOCTL;
	
	CLIENT_STATE = CLIENT_STATE_CONNECTED;

	return CLIENT_SUCCESS;
	}

int32_t CLIENT_Read(uint8_t *buffer, uint32_t buffer_size, int32_t *bytes) {
	int32_t err_code;
	
	if(CLIENT_STATE == CLIENT_STATE_CLOSED) {
		if((err_code = CLIENT_Connect()) != CLIENT_SUCCESS) {
			printf("CLI: CLIENT_Connect failed 0x%08X\n", err_code);
			return err_code;
			}
		else CLIENT_STATE = CLIENT_STATE_CONNECTED;
		}

	*bytes = read(CLIENT_SD, buffer, buffer_size);
	if(*bytes == 0) CLIENT_STATE = CLIENT_STATE_CLOSED;
	
	return CLIENT_SUCCESS;
	}

#define CLIENT_WRITE_RETRY_MAX			3

int32_t CLIENT_Write(uint8_t *buffer, uint32_t bytes) {
	int32_t err_code;
	int32_t err;
	uint8_t retries = 0;
	uint8_t OK = 0;
	
	if(CLIENT_STATE == CLIENT_STATE_CLOSED) {
		printf("CLI: Trying to establish Client Connection\n");
		if((err_code = CLIENT_Connect()) != CLIENT_SUCCESS) {
			printf("CLI: CLIENT_Connect failed 0x%08X\n", err_code);
			return err_code;
			}
		else {
			CLIENT_STATE = CLIENT_STATE_CONNECTED;
			printf("CLI: CLIENT_STATE_CONNECTED\n");
			}
		}
	
	do	{
		err_code = write(CLIENT_SD, buffer, bytes);
        
        if(err_code < 0) {
            if((errno == EAGAIN || errno == EWOULDBLOCK) && (retries < CLIENT_WRITE_RETRY_MAX)) {
                retries++;
                sleep(1);
            	}
            else break;
        	} 
        else {
            OK = 1;
            break;
        	}
		} while((retries < CLIENT_WRITE_RETRY_MAX) && !OK);
		
	if((retries >= CLIENT_WRITE_RETRY_MAX) && !OK) return CLIENT_ERROR_SOCKET_WRITE;
	else return CLIENT_SUCCESS;
	}

int32_t CLIENT_GET(char *file) {
	/* Caller passes ~120B path (ble.c); 2KB leaves 8x headroom. */
	char str[2048];
	int32_t err_code;

	err_code = snprintf(str, sizeof(str), "GET %s HTTP/1.1\r\n"
					"Host: %s\r\n"
					"Accept: */*\r\n"
					"\r\n",
					file, CLIENT_SERVER_NAME);

	if((err_code < 0) || (err_code >= (int32_t)sizeof(str))) {
		printf("CLI: ERROR get path too long\r\n");
		return CLIENT_ERROR_POST_STRING;
	}
					
	printf("CLI:\n");
	printf("CLI: *** SENT: START ***\n%s", str);
	printf("CLI: *** SENT: END ***\n");
	printf("CLI:\n");

	if((err_code = CLIENT_Write((uint8_t*)str, strlen(str))) != CLIENT_SUCCESS) {
		printf("CLI: CLIENT_Write failed 0x%08X\n", err_code);
		return err_code;
		}
	else return CLIENT_SUCCESS;
	}

int32_t CLIENT_POST(char *file, char *POST_str) {
	/* Path + JSON body are <1KB; 2KB + truncation check. */
	char str[2048];
	int32_t err_code;
	
	err_code = snprintf(str, sizeof(str), "POST %s HTTP/1.1\r\n"
					"Host: %s\r\n"
					"Content-Type: application/json\r\n"
					"Content-Length: %d\r\n"
					"Connection: keep-alive\r\n"
					"\r\n"
					"%s\r\n", 
					file, CLIENT_SERVER_NAME, (int)strlen(POST_str), POST_str);
					
	if((err_code < 0) || (err_code >= (int32_t)sizeof(str))) {
		printf("CLI: ERROR post data\r\n");
		return CLIENT_ERROR_POST_STRING;
		}
	
	printf("CLI:\n");
	printf("CLI: *** SENT: START ***\n%s", str);
	printf("CLI: *** SENT: END ***\n");
	printf("CLI:\n");

	if((err_code = CLIENT_Write((uint8_t*)str, strlen(str))) != CLIENT_SUCCESS) {
		printf("CLI: CLIENT_Write failed 0x%08X\n", err_code);
		return err_code;
		}
	else return CLIENT_SUCCESS;
	}

int32_t CLIENT_Close(void) {
	close(CLIENT_SD);
	CLIENT_STATE = CLIENT_STATE_CLOSED;

	return CLIENT_SUCCESS;
	}
