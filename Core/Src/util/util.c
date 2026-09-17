#include <string.h>
#include <stdio.h>
#include "string.h"
#include <stdlib.h>
#include <stdint.h>
#include "cmsis_os.h"
#include "stm32f7xx.h"

#include "util.h"

typedef struct {
	RNG_HandleTypeDef	hrng;
	osPoolId			queue_id;	// メモリプールID
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

void queue_init(struct queue *queue)
{
	UTIL_CB *this = get_myself();
	queue->head = NULL;
	queue->tail = NULL;
	queue->num = 0;
	// ネットワークプロトコル用のメモリプール確保
	osPoolDef(MemPool, 64, struct queue_entry);
	this->queue_id = osPoolCreate (osPool (MemPool));
	if (this->queue_id == NULL) {
		return;
	}
}

// プッシュ
struct queue_entry * queue_push(struct queue *queue, void *data)
{
	UTIL_CB *this = get_myself();
	struct queue_entry *entry;
	
	if (queue == NULL) {
		return NULL;
	}
	// メモリ確保
	entry = osPoolCAlloc(this->queue_id);
	if (entry == NULL) {
		return NULL;
	}
	entry->next = NULL;
	entry->data = data;
	if (queue->tail) {
		queue->tail->next = entry;
	}
	queue->tail = entry;
	if (!queue->head) {
		queue->head = entry;
	}
	queue->num++;
	
	return data;
}

// ポップ
void * queue_pop(struct queue *queue)
{
	UTIL_CB *this = get_myself();
	struct queue_entry *entry;
	void *data;
	
	if (!queue || !queue->head) {
		return NULL;
	}
	entry = queue->head;
	queue->head = entry->next;
	if (!queue->head) {
		queue->tail = NULL;
	}
	queue->num--;
	data = entry->data;
	osPoolFree(this->queue_id, entry);
	
	return data;
}

// 先頭データを返す
void * queue_peek(struct queue *queue)
{
	if (!queue || !queue->head) {
		return NULL;
	}
	return queue->head->data;
}

// 各エントリにデータを渡す
void queue_foreach(struct queue *queue, queue_func_t func, void *arg)
{
	struct queue_entry *entry;
	
	if (!queue || !func) {
		return;
	}
	for (entry = queue->head; entry; entry = entry->next) {
		func(arg, entry->data);
	}
}
