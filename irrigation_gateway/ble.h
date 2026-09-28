#ifndef __BLE_H__
#define __BLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BLE_ERROR_BASE
#define BLE_ERROR_BASE						0x10000000
#endif

#define	BLE_SUCCESS							0
#define	BLE_ERROR_TIMEOUT					(BLE_ERROR_BASE + 1)
#define	BLE_ERROR_INIT_DBUS					(BLE_ERROR_BASE + 10)
#define	BLE_ERROR_INIT_CONNECT_DBUS			(BLE_ERROR_BASE + 11)
#define BLE_ERROR_INIT_MESSAGE				(BLE_ERROR_BASE + 12)
#define BLE_ERROR_INIT_SETTING				(BLE_ERROR_BASE + 13)
#define	BLE_ERROR_SCAN_FLUSH_CONNECTION		(BLE_ERROR_BASE + 20)
#define	BLE_ERROR_SCAN_STOP					(BLE_ERROR_BASE + 21)
#define	BLE_ERROR_MESSAGE_ARGS				(BLE_ERROR_BASE + 30)
#define	BLE_ERROR_MESSAGE_GETALL			(BLE_ERROR_BASE + 31)
#define	BLE_ERROR_MESSAGE_REPLY				(BLE_ERROR_BASE + 32)
#define	BLE_ERROR_GET_DETAILS				(BLE_ERROR_BASE + 40)
#define	BLE_ERROR_CHAR_NOT_FOUND			(BLE_ERROR_BASE + 41)
#define BLE_ERROR_DESC_NOT_FOUND			(BLE_ERROR_BASE + 50)
#define	BLE_ERROR_CCCD_WRITE				(BLE_ERROR_BASE + 51)
#define	BLE_ERROR_START_NOTIFY				(BLE_ERROR_BASE + 52)
#define	BLE_ERROR_TRANSMIT					(BLE_ERROR_BASE + 60)

//#define BLE_NAME							"ALOHA"
#define BLE_NAME                            "Neko"
#define BLE_SCAN_DURATION					60
#define BLE_SERVICE_UUID					"0000ffe0-0000-1000-8000-00805f9b34fb"
#define BLE_CHARACTERISTIC_UUID				"0000ffe1-0000-1000-8000-00805f9b34fb"

typedef void (*BLE_DeviceFoundCallback)(const char *name, const char *mac, const char *path);
typedef void (*BLE_ReceiveCallback)(const uint8_t *data, size_t len);

extern char BLE_PATH[256];
extern uint8_t BLE_FOUND;
extern DBusConnection *BLE_CONN;

extern int32_t BLE_Init(void);
extern int32_t BLE_Scan_Start(void);
extern void BLE_Poll(BLE_DeviceFoundCallback cb);
extern int32_t BLE_Connect(const char *path);
extern int32_t BLE_Wait_For_Services_Resolved(const char *device_path, int64_t timeout_ms);
extern int32_t BLE_Characteristic_Find(const char *service_uuid, const char *char_uuid, char *char_path, size_t path_len);
extern int32_t BLE_Notification_Enable(const char *service_uuid, const char *char_uuid, BLE_ReceiveCallback callback);
extern int32_t BLE_Transmit(const char *service_uuid, const char *char_uuid, const uint8_t *data, uint32_t len);
extern void BLE_Receive(const uint8_t *data, size_t len);
extern int32_t BLE_Disconnect(void);

#ifdef __cplusplus
}
#endif

#endif
