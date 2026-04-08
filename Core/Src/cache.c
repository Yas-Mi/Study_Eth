#include <stdint.h>
#include <string.h>
#include "stm32f7xx.h"

void clean_dcache(void *addr, uint32_t size) {
	uintptr_t start = (uintptr_t)addr;
	uintptr_t end	= start + size;

	// 32byte境界に切り下げ
	start &= ~((uintptr_t)31);
	// 32byte境界に切り上げ
	end = (end + 31) & ~((uintptr_t)31);

	SCB_CleanDCache_by_Addr((uint32_t *)start, end - start);
}

