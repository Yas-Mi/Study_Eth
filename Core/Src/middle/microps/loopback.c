/*
 * loopback.c
 *
 *  Created on: Jul 6, 2026
 *      Author: hcuym
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cmsis_os.h"
#include "util.h"
#include "ip.h"
#include "net.h"

#define LOOPBACK_MTU	UINT16_MAX	// maximum size of IP datagram

// プロトタイプ
static osStatus loopback_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst);

static struct net_device_ops loopback_ops = {
	.output = loopback_output,
};

// 出力関数
static osStatus loopback_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
	debugf("dev=%s, type=%x, len=%d", dev->name, type, len);
	HEXDUMP(data, len);
	return net_input(type, data, len, dev);
}

struct net_device *loopback_init(void)
{
	struct net_device *dev;
	
	// ネットワークデバイスの割り当て
	dev = net_device_alloc();
	if (dev == NULL) {
		errorf("net_device_alloc() failure");
		return NULL;
	}
	// 設定
	dev->type = NET_DEVICE_TYPE_LOOPBACK;
	dev->mtu = LOOPBACK_MTU;
	dev->flags = NET_DEVICE_FLAG_LOOPBACK;
	dev->hlen = 0;
	dev->alen = 0;
	dev->ops = &loopback_ops;
	// 登録
	if (net_device_register(dev) != osOK) {
		errorf("net_device_register() failure");
		return NULL;
	}
	infof("success, dev=%s",  dev->name);
	
	return dev;
}
