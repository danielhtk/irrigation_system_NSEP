#include "global.h"

int32_t SYS_ERROR_NUM;

int32_t SYS_Init(void) {
	SYS_Error_Check(BLE_Init());		//init BLE
    SYS_Error_Check(CLIENT_Init());     //init CLIENT

	return SYS_SUCCESS;
	}
	
uint64_t SYS_MS(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL;
	}

void SYS_Fatal_Error(int32_t err, int32_t line, char* file) {
	printf("FATAL ERROR 0x%08X at line %d in file %s\n", err, line, file);
	exit(err);
	}

