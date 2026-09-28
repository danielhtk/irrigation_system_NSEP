#ifndef __SYS_H__
#define __SYS_H__

#ifdef __cplusplus
extern "C" {
#endif

#define SYS_ERROR_BASE			0x00002000
#define SYS_SUCCESS				0
#define SYS_ERROR_1				(SYS_ERROR_BASE + 1)		//0x00002001
#define SYS_ERROR_2				(SYS_ERROR_BASE + 2)		//0x00002002

#define SYS_Error_Check(x)		if((SYS_ERROR_NUM = (x)) != 0) SYS_Fatal_Error(SYS_ERROR_NUM, __LINE__, __FILE__)
#define SYS_TICK				SYS_MS()

extern void SYS_Fatal_Error(int32_t err, int32_t line, char* file);
extern int32_t SYS_Init(void);
extern uint64_t SYS_MS(void);

extern int32_t SYS_ERROR_NUM;

#ifdef __cplusplus
}
#endif

#endif
