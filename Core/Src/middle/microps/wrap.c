/*
 * wrap.c
 *
 *	Created on: Jul 21, 2026
 *		Author: hcuym
 */
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32f7xx.h"
#include "cmsis_os.h"
#include "console.h"
#include "portable.h"
#include "util.h"

osMailQId __wrap_osMailCreate (const osMailQDef_t *queue_def, osThreadId thread_id)
{
	size_t remain;
	osMailQId ret;
	
	// 実態を呼ぶ
	ret = __real_osMailCreate(queue_def, thread_id);
	
	// 残りサイズを表示
	remain = xPortGetFreeHeapSize();
	infof("remain %u bytes\n", remain);
	
	return ret;
}

osPoolId __wrap_osPoolCreate(const osPoolDef_t *pool_def)
{
	size_t remain;
	osPoolId ret;
	
	// 実態を呼ぶ
	ret = __real_osPoolCreate(pool_def);
	// 残りサイズを表示
	remain = xPortGetFreeHeapSize();
	infof("remain %u bytes\n", remain);
	
	return ret;
}
