#ifndef __CLIENT_H__
#define __CLIENT_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CLIENT_ERROR_BASE
#define CLIENT_ERROR_BASE					0x50000000
#endif

//return code enumeration
#define	CLIENT_SUCCESS						0
#define	CLIENT_ERROR_INIT					CLIENT_ERROR_BASE
#define	CLIENT_ERROR_OPEN_SOCKET			(CLIENT_ERROR_BASE + 0x01)
#define	CLIENT_ERROR_MEMORY_ALLOCATION		(CLIENT_ERROR_BASE + 0x02)
#define	CLIENT_ERROR_CONNECT				(CLIENT_ERROR_BASE + 0x03)
#define	CLIENT_ERROR_IOCTL					(CLIENT_ERROR_BASE + 0x04)
#define	CLIENT_ERROR_SSL_CTX				(CLIENT_ERROR_BASE + 0x05)
#define	CLIENT_ERROR_SSL_CREATE				(CLIENT_ERROR_BASE + 0x06)
#define	CLIENT_ERROR_SSL_FD					(CLIENT_ERROR_BASE + 0x07)
#define	CLIENT_ERROR_POST_STRING			(CLIENT_ERROR_BASE + 0x08)
#define	CLIENT_ERROR_SOCKET_WRITE			(CLIENT_ERROR_BASE + 0x09)

#define CLIENT_SERVER_NAME_DEFAULT			"localhost"
#define	CLIENT_SERVER_PORT_DEFAULT			80

typedef enum {
	CLIENT_STATE_CLOSED,
	CLIENT_STATE_CONNECTED
	} CLIENT_STATE_e;

extern char CLIENT_SERVER_NAME[80];
extern uint16_t CLIENT_SERVER_PORT;
extern CLIENT_STATE_e CLIENT_STATE;
extern int32_t CLIENT_SD;

extern int32_t CLIENT_Init(void);
extern int32_t CLIENT_Connect(void);
extern int32_t CLIENT_Read(uint8_t *buffer, uint32_t buffer_size, int32_t *bytes);
extern int32_t CLIENT_Write(uint8_t *buffer, uint32_t bytes);
extern int32_t CLIENT_GET(char *file);
extern int32_t CLIENT_POST(char *file, char *POST_str);
extern int32_t CLIENT_Close(void);

#ifdef __cplusplus
}
#endif

#endif
