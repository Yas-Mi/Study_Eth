/*
 * arp.c
 *
 *  Created on: Aug 20, 2026
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

#include "arp.h"

struct arp_hdr {
	uint16_t	hrd;
	uint16_t	pro;
	uint8_t		hln;
	uint8_t		pln;
	uint16_t	op;
};

// ip_addr_tを使わない理由：arp_hdrが8byteなので32bit境界にそろうが、shaはマックアドレスで48bitのため、そのあとのspaとの間にpaddingができてしまうため
struct arp_ether_ip {
	struct arp_hdr hdr;				// 
	uint8_t	sha[ETHER_ADDR_LEN];
	uint8_t	spa[IP_ADDR_LEN];
	uint8_t	tha[ETHER_ADDR_LEN];
	uint8_t	tpa[IP_ADDR_LEN];
};

#define ARP_HRD_ETHER	(0x0001)
#define ARP_PRO_IP		ETHER_TYPE_IP

#define ARP_OP_REQUEST	(1)
#define ARP_OP_REPLY	(2)

static char *arp_opcode_ntoa(uint16_t opcode)
{
	switch (ntoh16(opcode)) {
	case ARP_OP_REQUEST:
		return "Request";
	case ARP_OP_REPLY:
		return "Reply";
	}
	return "Unlnown";
}

static void arp_print(const uint8_t *data, size_t len)
{
	struct arp_ether_ip *message;
	ip_addr_t spa, tpa;
	char addr[ETHER_ADDR_STR_LEN];
	
	message = (struct arp_ether_ip *)data;
	
	debugf("hrd=0x%x", ntoh16(message->hdr.hrd));
	debugf("pro=0x%x", ntoh16(message->hdr.pro));
	debugf("hln=%d", message->hdr.hln);
	debugf("pln=%d", message->hdr.pln);
	debugf("op=%d (%s)", ntoh16(message->hdr.op), arp_opcode_ntoa(message->hdr.op));
	debugf("sha:%s", ether_drv_addr_ntop(message->sha, addr, sizeof(addr)));
	memcpy(&spa, message->spa, sizeof(spa));
	debugf("spa:%s", ip_addr_ntop(spa, addr, sizeof(addr)));
	debugf("tha:%s", ether_drv_addr_ntop(message->tha, addr, sizeof(addr)));
	memcpy(&tpa, message->tpa, sizeof(tpa));
	debugf("tpa:%s", ip_addr_ntop(tpa, addr, sizeof(addr)));
};

static osStatus arp_reply(struct net_iface *iface, const uint8_t *tha, ip_addr_t tpa)
{
	struct arp_ether_ip reply;
	
	reply.hdr.hrd = hton16(ARP_HRD_ETHER);
	reply.hdr.pro = hton16(ARP_PRO_IP);
	reply.hdr.hln = ETHER_ADDR_LEN;
	reply.hdr.pln = IP_ADDR_LEN;
	reply.hdr.op = hton16(ARP_OP_REPLY);
	memcpy(reply.sha, iface->dev->addr, ETHER_ADDR_LEN);
	memcpy(reply.spa, &((struct ip_iface*)iface)->unicast, IP_ADDR_LEN);
	memcpy(reply.tha, tha, ETHER_ADDR_LEN);
	memcpy(reply.tpa, &tpa, IP_ADDR_LEN);
	debugf("dev=%s, len=%d", iface->dev->name, sizeof(reply));
	arp_print((uint8_t*)&reply, sizeof(reply));
	return net_device_output(iface->dev, ETHER_TYPE_ARP, (uint8_t*)&reply, sizeof(reply), tha);
}

// ARPメッセージの入力
static void arp_input(const uint8_t *data, size_t len, struct net_device *dev)
{
	struct arp_ether_ip *msg;
	ip_addr_t spa, tpa;
	struct net_iface *iface;
	
	// サイズチェック
	if (len < sizeof(*msg)) {
		errorf("too short");
		return;
	}
	msg = (struct arp_ether_ip *)data;
	// アドレスタイプのチェック (ハードウェアアドレス、プロトコルアドレスが、それぞれEthermetとIPであるかどうか)
	if ((ntoh16(msg->hdr.hrd) != ARP_HRD_ETHER) || (msg->hdr.hln != ETHER_ADDR_LEN)) {
		errorf("unsupport hardware address");
		return;
	}
	if ((ntoh16(msg->hdr.pro) != ARP_PRO_IP) || (msg->hdr.pln != IP_ADDR_LEN)) {
		errorf("unsupport protocol address");
		return;
	}
	debugf("dev=%s, len=%d", dev->name, len);
	arp_print(data, len);
	memcpy(&spa, msg->spa, sizeof(spa));
	memcpy(&tpa, msg->tpa, sizeof(tpa));
	// IPインタフェースを取得
	iface = net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
	// 自身宛ての場合は、ARP応答
	if (iface && ((struct ip_iface*)iface)->unicast == tpa) {
		if (ntoh16(msg->hdr.op) == ARP_OP_REQUEST) {
			arp_reply(iface, msg->sha, spa);
		}
	}
}

osStatus arp_init(void)
{
	osStatus ercd;
	
	// 入力ハンドラの登録
	if ((ercd = net_protocol_register(NET_PROTOCOL_TYPE_ARP, arp_input)) != osOK) {
		errorf("net_protocol_register() failed");
	}
	return ercd;
}
