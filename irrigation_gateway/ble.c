#include "global.h"

char BLE_PATH[256];
uint8_t BLE_FOUND = 0;
DBusConnection *BLE_CONN;

BLE_ReceiveCallback recv_cb = NULL;

int get_device_info(const char *path, char *name_out, size_t name_size, char *mac_out, size_t mac_size) {
	DBusMessage *msg, *reply;
	DBusMessageIter args, dict;
	DBusError err;
	int got_name = 0, got_mac = 0;
	const char *iface = "org.bluez.Device1";
	DBusMessageIter entry, var;
	const char *key;    
    const char *val;
    
    dbus_error_init(&err);

    msg = dbus_message_new_method_call("org.bluez", path, "org.freedesktop.DBus.Properties", "GetAll");
    if(!msg) return BLE_ERROR_INIT_MESSAGE;

	if(!dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_INVALID)) {
		dbus_message_unref(msg);
		return BLE_ERROR_MESSAGE_ARGS;
		}

	reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
	dbus_message_unref(msg);

	if(!reply) {
		if(dbus_error_is_set(&err)) {
			printf("BLE: GetAll error: %s - %s\n", err.name, err.message);
			dbus_error_free(&err);
			}
		return BLE_ERROR_MESSAGE_GETALL;
		}

	if(dbus_message_iter_init(reply, &args) && (DBUS_TYPE_ARRAY == dbus_message_iter_get_arg_type(&args))) {
		dbus_message_iter_recurse(&args, &dict);
        
		while(dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
            dbus_message_iter_recurse(&dict, &entry);

            if(DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&entry)) {
				dbus_message_iter_get_basic(&entry, &key);

                dbus_message_iter_next(&entry);
                if(DBUS_TYPE_VARIANT == dbus_message_iter_get_arg_type(&entry)) {
                    dbus_message_iter_recurse(&entry, &var);

                    if(strcmp(key, "Name") == 0 && DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&var)) {
                        dbus_message_iter_get_basic(&var, &val);
                        snprintf(name_out, name_size, "%s", val);
                        got_name = 1;
                    	}
                    else if(strcmp(key, "Address") == 0 && DBUS_TYPE_STRING == dbus_message_iter_get_arg_type(&var)) {
                        dbus_message_iter_get_basic(&var, &val);
                        snprintf(mac_out, mac_size, "%s", val);
                        got_mac = 1;
						}
                	}
            	}
            dbus_message_iter_next(&dict);
        	}
    	}

    dbus_message_unref(reply);
    return (got_name && got_mac) ? BLE_SUCCESS : BLE_ERROR_GET_DETAILS;
	}

static DBusHandlerResult notification_handler(DBusConnection *conn, DBusMessage *msg, void *user_data) {
    DBusMessageIter args, array;
    const char *interface, *property;
    unsigned char byte;
    uint8_t buffer[512];
    size_t len = 0;

    // Only handle PropertiesChanged signals
    if(!dbus_message_is_signal(msg, "org.freedesktop.DBus.Properties", "PropertiesChanged")) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_message_iter_init(msg, &args);
    dbus_message_iter_get_basic(&args, &interface);

    // Check if this is a GattCharacteristic1 notification
    if(strcmp(interface, "org.bluez.GattCharacteristic1") != 0) return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    // Move to changed properties dictionary
    dbus_message_iter_next(&args);
    DBusMessageIter dict;
    dbus_message_iter_recurse(&args, &dict);

    // Loop through changed properties
    while(dbus_message_iter_get_arg_type(&dict) != DBUS_TYPE_INVALID) {
        DBusMessageIter entry, value_variant, array_iter;
        dbus_message_iter_recurse(&dict, &entry);

        // property name
        dbus_message_iter_get_basic(&entry, &property);

        if(strcmp(property, "Value") == 0) {
            // Move to variant
            dbus_message_iter_next(&entry);
            dbus_message_iter_recurse(&entry, &value_variant);

            // Recurse into the array of bytes
            dbus_message_iter_recurse(&value_variant, &array_iter);

            len = 0;
            while(dbus_message_iter_get_arg_type(&array_iter) != DBUS_TYPE_INVALID) {
                dbus_message_iter_get_basic(&array_iter, &byte);
                buffer[len++] = byte;
                dbus_message_iter_next(&array_iter);
            	}

            if(recv_cb) recv_cb(buffer, len);								// Call registered callback
            }

        dbus_message_iter_next(&dict);
    	}

    return DBUS_HANDLER_RESULT_HANDLED;
	}

int32_t BLE_Init(void) {
	DBusError err;
	const char *match = "type='signal',interface='org.freedesktop.DBus.ObjectManager',member='InterfacesAdded'";
	
	dbus_error_init(&err);													//prepare DBusError structure for usage
	BLE_CONN = dbus_bus_get(DBUS_BUS_SYSTEM, &err);							//connect to the system DBus
	
	dbus_connection_add_filter(BLE_CONN, notification_handler, NULL, NULL);	
	dbus_bus_add_match(BLE_CONN, "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',arg0='org.bluez.GattCharacteristic1'", NULL);

	if(dbus_error_is_set(&err)) {											//check if DBusError was set (set when last DBus function failed)
		printf("BLE: ERROR get DBUS, %s - %s\n", err.name, err.message);
		dbus_error_free(&err);												//free memory allocated to DBusError
		return BLE_ERROR_INIT_DBUS;
		}
		
	if(!BLE_CONN) {															//if not connected to DBus, error
		printf("BLE: ERROR failed to connect to DBus\n");
		return BLE_ERROR_INIT_CONNECT_DBUS;
		}
		
	//listen for InterfacesAdded signals
    dbus_bus_add_match(BLE_CONN, match, &err);
    dbus_connection_flush(BLE_CONN);

    if(dbus_error_is_set(&err)) {
        printf("BLE: Match add error: %s - %s\n", err.name, err.message);
        dbus_error_free(&err);
        return BLE_ERROR_SCAN_FLUSH_CONNECTION;
   		}

	return BLE_SUCCESS;
	}
	
int32_t BLE_Scan_Start(void) {
    DBusMessage *msg;
    DBusError err;
    
    dbus_error_init(&err);
    
	msg = dbus_message_new_method_call("org.bluez", "/org/bluez/hci0", "org.bluez.Adapter1", "StartDiscovery");
    if(!msg) return BLE_ERROR_INIT_MESSAGE;

    if(!dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err)) {
        printf("BLE: StartDiscovery error: %s - %s\n", err.name, err.message);
        dbus_error_free(&err);
        dbus_message_unref(msg);
        return BLE_ERROR_INIT_SETTING;
    	}

    dbus_message_unref(msg);
	return BLE_SUCCESS;
	}

void BLE_Poll(BLE_DeviceFoundCallback cb) {
    DBusMessage *msg;
    DBusMessageIter args;
    const char *path;
	char name[256] = {0};
	char mac[64] = {0};

    dbus_connection_read_write(BLE_CONN, 100);
    msg = dbus_connection_pop_message(BLE_CONN);
    if(!msg) return;

    if(dbus_message_is_signal(msg, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded")) {
		dbus_message_iter_init(msg, &args);
		dbus_message_iter_get_basic(&args, &path);

		if(strncmp(path, "/org/bluez/hci0/dev_", 20) == 0) {
			if(get_device_info(path, name, sizeof(name), mac, sizeof(mac)) == 0) {
				if(cb) cb(name, mac, path);
				}
        	}
		}

    dbus_message_unref(msg);
	}
	
int32_t BLE_Connect(const char *path) {
	DBusMessage *msg, *reply;
	DBusError err;

	dbus_error_init(&err);

	msg = dbus_message_new_method_call("org.bluez", path, "org.bluez.Device1", "Connect");
	if(!msg) return BLE_ERROR_INIT_MESSAGE;

	reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
	dbus_message_unref(msg);

	if(!reply) {
		if(dbus_error_is_set(&err)) {
			printf("BLE: Connect error: %s - %s\n", err.name, err.message);
			dbus_error_free(&err);
			}
	
		return BLE_ERROR_MESSAGE_REPLY;
		}

	dbus_message_unref(reply);
	return BLE_SUCCESS;
	}
	
int32_t BLE_Wait_For_Services_Resolved(const char *device_path, int64_t timeout_ms) {
	uint64_t timeout = SYS_TICK + timeout_ms;
	const char *iface = "org.bluez.Device1";
	const char *prop = "ServicesResolved";
    DBusMessage *msg, *reply;
    DBusError err;
    DBusMessageIter iter, variant;
	dbus_bool_t resolved = FALSE;

    dbus_error_init(&err);

    while(SYS_TICK < timeout) {
        msg = dbus_message_new_method_call("org.bluez", device_path, "org.freedesktop.DBus.Properties", "Get");
        if(!msg) return BLE_ERROR_INIT_MESSAGE;
        	
        dbus_message_append_args(msg, DBUS_TYPE_STRING, &iface, DBUS_TYPE_STRING, &prop, DBUS_TYPE_INVALID);

        reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
        dbus_message_unref(msg);

        if(!reply) {
            dbus_error_free(&err);
            return BLE_ERROR_MESSAGE_REPLY;
        	}

        dbus_message_iter_init(reply, &iter);
        dbus_message_iter_recurse(&iter, &variant);
       
        if(dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_BOOLEAN) dbus_message_iter_get_basic(&variant, &resolved);
        dbus_message_unref(reply);

        if(resolved) return BLE_SUCCESS;
        usleep(200 * 1000); // Wait 200 ms
	    }

    return BLE_ERROR_TIMEOUT;
	}
	
int32_t BLE_Characteristic_Find(const char *service_uuid, const char *char_uuid, char *char_path, size_t path_len) {
    DBusMessage *msg, *reply;
    DBusMessageIter iter, dict, entry, iface_entry, prop_entry, variant;
    DBusError err;
    const char *obj_path;
    char service_path[256] = {0};
	const char *iface_name;
	const char *prop_name;
	const char *uuid;	
	
    dbus_error_init(&err);

    // Get all managed objects
    msg = dbus_message_new_method_call("org.bluez", "/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    if(!msg) return BLE_ERROR_INIT_MESSAGE;

    reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
    dbus_message_unref(msg);

    if(!reply) {
        dbus_error_free(&err);
        return BLE_ERROR_MESSAGE_REPLY;
	    }

    if(!dbus_message_iter_init(reply, &iter) ||
       dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY) {
       	dbus_message_unref(reply);
       	return BLE_ERROR_MESSAGE_ARGS;
		}

    // Iterate over object paths
    dbus_message_iter_recurse(&iter, &dict);
    while(dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
        dbus_message_iter_recurse(&dict, &entry);

        // Object path
        if(dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_OBJECT_PATH) {
           dbus_message_iter_get_basic(&entry, &obj_path);
           dbus_message_iter_next(&entry);

            // Interfaces
            if(dbus_message_iter_get_arg_type(&entry) == DBUS_TYPE_ARRAY) {
                DBusMessageIter ifaces;
                dbus_message_iter_recurse(&entry, &ifaces);

                while(dbus_message_iter_get_arg_type(&ifaces) == DBUS_TYPE_DICT_ENTRY) {
                    dbus_message_iter_recurse(&ifaces, &iface_entry);

                    // Interface name
                    if(dbus_message_iter_get_arg_type(&iface_entry) == DBUS_TYPE_STRING) {
                        dbus_message_iter_get_basic(&iface_entry, &iface_name);
                        dbus_message_iter_next(&iface_entry);

                        // Match GattService1 with service_uuid
                        if(strcmp(iface_name, "org.bluez.GattService1") == 0) {
                            if(dbus_message_iter_get_arg_type(&iface_entry) == DBUS_TYPE_ARRAY) {
                                DBusMessageIter props;
                                dbus_message_iter_recurse(&iface_entry, &props);

                                while(dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                                    dbus_message_iter_recurse(&props, &prop_entry);
                                    dbus_message_iter_get_basic(&prop_entry, &prop_name);
                                    dbus_message_iter_next(&prop_entry);

                                    if(strcmp(prop_name, "UUID") == 0) {
                                        dbus_message_iter_recurse(&prop_entry, &variant);
    
                                        if(dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                                            dbus_message_iter_get_basic(&variant, &uuid);
    
                                            if(strcasecmp(uuid, service_uuid) == 0) {
                                                strncpy(service_path, obj_path, sizeof(service_path)-1);
    	                                        }
    	                                    }
    	                                }
                                    dbus_message_iter_next(&props);
    	                            }
	                            }
							}

                        // Match GattCharacteristic1 under the service_path with char_uuid
                        if(service_path[0] &&
                           strcmp(iface_name, "org.bluez.GattCharacteristic1") == 0 &&
                           strncmp(obj_path, service_path, strlen(service_path)) == 0) {

                            if(dbus_message_iter_get_arg_type(&iface_entry) == DBUS_TYPE_ARRAY) {
                                DBusMessageIter props;
                                dbus_message_iter_recurse(&iface_entry, &props);

                                while(dbus_message_iter_get_arg_type(&props) == DBUS_TYPE_DICT_ENTRY) {
                                    dbus_message_iter_recurse(&props, &prop_entry);
                                    dbus_message_iter_get_basic(&prop_entry, &prop_name);
                                    dbus_message_iter_next(&prop_entry);

                                    if(strcmp(prop_name, "UUID") == 0) {
                                        dbus_message_iter_recurse(&prop_entry, &variant);

                                        if(dbus_message_iter_get_arg_type(&variant) == DBUS_TYPE_STRING) {
                                            dbus_message_iter_get_basic(&variant, &uuid);

                                            if(strcasecmp(uuid, char_uuid) == 0) {
                                                strncpy(char_path, obj_path, path_len-1);
                                                dbus_message_unref(reply);
                                                return BLE_SUCCESS;
	                                            }
	                                        }
	                                    }
                                    dbus_message_iter_next(&props);
	                                }
	                            }
	                        }
	                    }
                    dbus_message_iter_next(&ifaces);
	                }
	            }
	        }
        dbus_message_iter_next(&dict);
	    }

    dbus_message_unref(reply);
    return BLE_ERROR_CHAR_NOT_FOUND;
	}

int32_t BLE_Notification_Enable(const char *service_uuid, const char *char_uuid, BLE_ReceiveCallback callback) {
    char char_path[256];
    DBusMessage *msg, *reply;
    DBusError err;
    	
    SYS_Error_Check(BLE_Characteristic_Find(service_uuid, char_uuid, char_path, sizeof(char_path)));
    
    dbus_error_init(&err);
    
	// Create a method call to StartNotify on the characteristic interface
    msg = dbus_message_new_method_call("org.bluez", char_path, "org.bluez.GattCharacteristic1", "StartNotify");
    if(!msg) return BLE_ERROR_INIT_MESSAGE;

    // Send message and wait for reply
    reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
    dbus_message_unref(msg);

    if(dbus_error_is_set(&err)) {
        dbus_error_free(&err);
        if(reply) dbus_message_unref(reply);
        return BLE_ERROR_START_NOTIFY;
	    }

    if(reply) dbus_message_unref(reply);

    recv_cb = callback;
    return BLE_SUCCESS;	
	}
	
int32_t BLE_Transmit(const char *service_uuid, const char *char_uuid, const uint8_t *data, uint32_t len) {
    char char_path[256];
    DBusMessage *msg = NULL;
    DBusMessage *reply = NULL;
    DBusMessageIter args, array, dict, entry, variant;
    DBusError err;
    uint32_t i;
    unsigned char b;
    const char *key = "type";
    const char *type_value = "command";

    dbus_error_init(&err);

    // Find the GATT characteristic path
    if(BLE_Characteristic_Find(service_uuid, char_uuid, char_path, sizeof(char_path)) != BLE_SUCCESS) return BLE_ERROR_CHAR_NOT_FOUND;

    // Create method call to WriteValue
    msg = dbus_message_new_method_call("org.bluez", char_path, "org.bluez.GattCharacteristic1", "WriteValue");
    if(!msg) return BLE_ERROR_INIT_MESSAGE;

    dbus_message_iter_init_append(msg, &args);

    // First argument: array of bytes
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "y", &array);
    for(i = 0; i < len; i++) {
		b = data[i];
        dbus_message_iter_append_basic(&array, DBUS_TYPE_BYTE, &b);
    	}
    dbus_message_iter_close_container(&args, &array);

	// Second argument: options dict { "type": "request" }
    dbus_message_iter_open_container(&args, DBUS_TYPE_ARRAY, "{sv}", &dict);
    dbus_message_iter_open_container(&dict, DBUS_TYPE_DICT_ENTRY, NULL, &entry);
    dbus_message_iter_append_basic(&entry, DBUS_TYPE_STRING, &key);
    dbus_message_iter_open_container(&entry, DBUS_TYPE_VARIANT, "s", &variant);
    dbus_message_iter_append_basic(&variant, DBUS_TYPE_STRING, &type_value);
    dbus_message_iter_close_container(&entry, &variant);
    dbus_message_iter_close_container(&dict, &entry);
    dbus_message_iter_close_container(&args, &dict);

    // Send and wait for reply
    reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
    dbus_message_unref(msg);

    if(dbus_error_is_set(&err)) {
        printf("BLE: WriteValue error: %s - %s\n", err.name, err.message);
        dbus_error_free(&err);
        if(reply) dbus_message_unref(reply);
        return BLE_ERROR_TRANSMIT;
    	}

    if(reply) dbus_message_unref(reply);

    return BLE_SUCCESS;
	}
	
void BLE_Receive(const uint8_t *data, size_t len) {
    //printf("Received %zu bytes: ", len);
    //for(size_t i = 0; i < len; i++) printf("%02X ", data[i]);
    //printf("\n");
	DATA_TYPE_S received;
    uint8_t temp;
    char send_string[200];
    

    memcpy(&received.byte[0], data, len);
    printf("id_sn=%lu, value=%f, data_type=%u\n",
            received.field.id_sn,
            received.field.value,
            received.field.data_type);
    sprintf(send_string, "/POST_data.php?datetime=%lu&id_gw=%lu&id_sn=%lu&value=%f&data_type=%u",
            time(NULL),
            ID_GW,
            received.field.id_sn,
            received.field.value,
            received.field.data_type);

    SYS_Error_Check(CLIENT_GET(send_string));
    }
	
int32_t BLE_Disconnect(void) {
    DBusMessage *msg, *reply;
    DBusError err;

    dbus_error_init(&err);

    msg = dbus_message_new_method_call("org.bluez", BLE_PATH, "org.bluez.Device1", "Disconnect");
    if(!msg) return BLE_ERROR_INIT_MESSAGE;

    reply = dbus_connection_send_with_reply_and_block(BLE_CONN, msg, -1, &err);
    dbus_message_unref(msg);

    if(dbus_error_is_set(&err)) {
        printf("BLE_Disconnect: %s - %s\n", err.name, err.message);
        dbus_error_free(&err);
        if(reply) dbus_message_unref(reply);
        return BLE_ERROR_MESSAGE_REPLY;
    	}

    if(reply) dbus_message_unref(reply);
    
    return BLE_SUCCESS;
	}

