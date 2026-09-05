#include <string.h>
#include <stdio.h>
#include "string.h"
#include <stdlib.h>
#include <stdint.h>
#include "cmsis_os.h"
#include "stm32f7xx.h"

#include "util.h"

typedef struct {
	RNG_HandleTypeDef hrng;
} UTIL_CB;
static UTIL_CB util_cb;
#define get_myself()	&util_cb

osStatus util_init(void)
{
	UTIL_CB *this = get_myself();
	
	// 制御ブロック初期化
	memset(this, 0, sizeof(UTIL_CB));
	// 初期化
	this->hrng.Instance = RNG;
	if (HAL_RNG_Init(&(this->hrng)) != HAL_OK)
	{
		while(1){};
	}
	return osOK;
}

// チェックサム計算
uint16_t cksum16(uint16_t *addr, uint16_t count, uint32_t init)
{
	uint32_t sum;
	
	sum = init;
	while (count > 1) {
		sum += *(addr++);
		count -= 2;
	}
	
	if (count > 0) {
		sum += *(uint8_t*)addr;
	}
	while (sum >> 16) {
		sum = (sum & 0xFFFF) + (sum >> 16);
	}
	return ~(uint16_t)sum;
}

// ネットワークバイトオーダー
uint16_t hton16(uint16_t h)
{
	uint16_t ret = 0;
	
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // little
	ret = (((h & 0x00FF) << 8) | ((h & 0xFF00) >> 8));
#else
    // big
	ret = h;
#endif
	
	return ret;
}

uint16_t ntoh16(uint16_t n)
{
	uint16_t ret = 0;
	
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // little
	ret = (((n & 0x00FF) << 8) | ((n & 0xFF00) >> 8));
#else
    // big
	ret = n;
#endif
	
	return ret;
}

uint32_t hton32(uint32_t h)
{
	uint32_t ret = 0;
	
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // little
	ret = (((h & 0x000000FF) << 24) | ((h & 0x0000FF00) << 8) | ((h & 0x00FF0000) >> 8) | ((h & 0xFF000000) >> 24));
#else
    // big
	ret = h;
#endif
	
	return ret;
}

uint32_t ntoh32(uint32_t n)
{
	uint32_t ret = 0;
	
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    // little
	ret = (((n & 0x000000FF) << 24) | ((n & 0x0000FF00) << 8) | ((n & 0x00FF0000) >> 8) | ((n & 0xFF000000) >> 24));
#else
    // big
	ret = n;
#endif
	
	return ret;
}

uint16_t rondom16(void)
{
	UTIL_CB *this = get_myself();
	uint32_t rondom;
	
	// 乱数取得
	if (HAL_RNG_GenerateRandomNumber(&(this->hrng), &rondom) != HAL_OK) {
		rondom = 0xFFFF;
	}
	
	return (uint16_t)rondom;
}
