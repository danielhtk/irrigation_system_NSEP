#ifndef __GLOBAL_H__
#define __GLOBAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <dbus/dbus.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

#include <sys/socket.h>
#include <poll.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/ioctl.h>
#include <errno.h>

#include "ble.h"
#include "sys.h"
#include "client.h"

#ifdef __cplusplus
}
#endif

#endif

#define ID_GW 0x998877665544

typedef union {
    struct {
        uint64_t id_sn;
        float    value;
        uint8_t  data_type;
    } field;
    uint8_t byte[13];
}  DATA_TYPE_S;
