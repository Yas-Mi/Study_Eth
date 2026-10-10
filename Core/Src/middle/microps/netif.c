/*
 * netif.c
 *
 *  Created on: Jul 27, 2026
 *      Author: hcuym
 */
#include <string.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32f7xx.h"
#include "cmsis_os.h"
#include "console.h"
#include "ip.h"
#include "net.h"
#include "util.h"
#include "eth_drv.h"

#include "netif.h"

#define NETIF_DBG_PRINT(fmt, ...) debugf("[NIF ]:"fmt,  ##__VA_ARGS__)
#define NETIF_INFO_PRINT(fmt, ...) infof("[NIF ]:"fmt,  ##__VA_ARGS__)
#define NETIF_ERR_PRINT(fmt, ...) errorf("[NIF ]:"fmt,  ##__VA_ARGS__)

// 制御ブロック
typedef struct {
	osPoolId			iface_id;								// ネットワークデバイス用のメモリプールID
	osPoolId			protocol_id;							// プロトコル用のメモリプールID
	struct ip_iface 	*ifaces;								// 連結リスト
	struct ip_protocol	*protocols;								// 連結リスト
	uint8_t				send_frame[ETHER_FRAME_SIZE_MAX];		// 送信フレーム
}NEIF_CB;
static NEIF_CB netif_cb;
#define get_myself() (&netif_cb)

static osStatus netif_open(struct net_device *dev);
static osStatus netif_close(struct net_device *dev);
osStatus netif_output(struct net_device *dev, uint16_t type, const uint8_t *buf, size_t len, const void *dst);

static struct net_device_ops netif_ops = {
	.open = netif_open,
	.close = netif_close,
	.output = netif_output,
};

struct ether_hdr {
	uint8_t dst[ETHER_ADDR_LEN];
	uint8_t src[ETHER_ADDR_LEN];
	uint16_t type;
};

static const ETH_DRV_CH netif_ch_mapping_tbl[] = {
	ETH_DRV_CH_1,
};

struct net_device *netif_init(char *name, const char *addr)
{
	NEIF_CB *this = get_myself();
	struct net_device *dev;
	
	NETIF_INFO_PRINT("name=%s, addr=%s", name, addr ? addr : "(none)");
	dev = net_device_alloc();
	if (dev == NULL) {
		NETIF_ERR_PRINT("net_device_alloc() failure");
		return NULL;
	}
	// 設定
	dev->type = NET_DEVICE_TYPE_ETHRNET;
	dev->mtu = ETHER_PAYLOAD_SIZE_MAX;
	dev->flags = (NET_DEVICE_FLAG_BROADCAST | NET_DEVICE_FLAG_NEED_ARP);
	dev->hlen = ETHER_HDR_SIZE;
	dev->alen = ETHER_ADDR_LEN;
	memcpy(dev->bloadcast, ETHER_ADDR_BROADCAST, ETHER_ADDR_LEN);
	if (addr != NULL) {
		if (ether_drv_addr_pton(addr, dev->addr) != 0) {
			NETIF_ERR_PRINT("invalid address, addr=%s", addr);
			return NULL;
		}
		
	}
	dev->ops = &netif_ops;
	dev->priv = &(netif_ch_mapping_tbl[0]);
	// 登録
	if (net_device_register(dev) != osOK) {
		NETIF_ERR_PRINT("net_device_register() failure");
		return NULL;
	}
	NETIF_INFO_PRINT("success, dev=%s", dev->name);
	
	return dev;
}

static osStatus netif_set_default_addr(struct net_device *dev)
{
	// 特に何もしない
	return osOK;
}

// オープン
static osStatus netif_open(struct net_device *dev)
{
	ETH_DRV_CH ch;
	osStatus ercd;
	
	// ch情報取得
	ch = *((ETH_DRV_CH*)(dev->priv));
	
	// 登録
	if ((ercd = eth_drv_open(ch, (char*)dev->addr, dev)) != osOK) {
		NETIF_ERR_PRINT("net_device_register() failure");
		goto NETIF_OPEN_END;
	}
	
NETIF_OPEN_END:
	return ercd;
}

// クローズ
static osStatus netif_close(struct net_device *dev)
{
	ETH_DRV_CH ch;
	osStatus ercd;
	
	// ch情報取得g
	ch = *((ETH_DRV_CH*)(dev->priv));
	
	// いずれクローズ作ります
	
	return ercd;
}

osStatus netif_output(struct net_device *dev, uint16_t type, const uint8_t *buf, size_t len, const void *dst)
{
	NEIF_CB *this = get_myself();
	uint8_t *frame = this->send_frame;
	struct ether_hdr *hdr;
	size_t flen, pad = 0;
	ETH_DRV_CH ch;
	
	hdr = (struct ether_hdr *)frame;
	memcpy(hdr->dst, dst, ETHER_ADDR_LEN);
	memcpy(hdr->src, dev->addr, ETHER_ADDR_LEN);
	hdr->type = hton16(type);
	memcpy(hdr+1, buf, len);
	if (len < ETHER_PAYLOAD_SIZE_MIN) {
		pad = ETHER_PAYLOAD_SIZE_MIN - len;
	}
	flen = sizeof(*hdr) + len + pad;
	NETIF_DBG_PRINT("dev=%s, type=%x, len=%d", dev->name, type, flen);
	netif_print(frame, flen);
	
	// ch情報取得
	ch = *((ETH_DRV_CH*)(dev->priv));
	
	return eth_drv_send(ch, frame, flen, 1000);
}

// 入力
osStatus netif_input(struct net_device *dev, uint8_t *frame, size_t flen)
{
	struct ether_hdr *hdr;
	uint16_t type;
	
	// 長さチェック
	if (flen < (ssize_t)sizeof(*hdr)) {
		NETIF_ERR_PRINT("too short");
		return osErrorParameter;
	}
	// 宛先の検証
	hdr = (struct ether_hdr *)frame;
	// フレームの宛先アドレスと自身のアドレスが一致していない場合
	if (memcmp(dev->addr, hdr->dst, ETHER_ADDR_LEN) != 0) {
		// ブロードキャストでない場合
		if (memcmp(ETHER_ADDR_BROADCAST, hdr->dst, ETHER_ADDR_LEN) != 0) {
			// for other host
			return osErrorParameter;
		}
	}
	type = ntoh16(hdr->type);
	NETIF_DBG_PRINT("dev=%s, type=0x%x, len=%d", dev->name, type, flen);
	netif_print(frame, flen);
	
	// プロトコルスタックへ引き渡す
	return net_input(type, (uint8_t*)(hdr+1), flen - sizeof(*hdr), dev);
}

// ethernetフレームの詳細出力
void netif_print(const uint8_t *frame, size_t flen)
{
	struct ether_hdr *hdr;
	char addr[ETHER_ADDR_STR_LEN];
	
	hdr = (struct ether_hdr*)frame;
	
	NETIF_DBG_PRINT("src=%s,", ether_drv_addr_ntop(hdr->src, addr, sizeof(addr)));
	NETIF_DBG_PRINT("dst=%s", ether_drv_addr_ntop(hdr->dst, addr, sizeof(addr)));
}
