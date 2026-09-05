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

struct arp_cache {
	unsigned char	state;					// 状態
	ip_addr_t		pa;						// IPアドレス
	uint8_t			ha[ETHER_ADDR_LEN];		// MACアドレス
	uint32_t		timestamp;				// タイムスタンプ
};

#define ARP_HRD_ETHER	(0x0001)
#define ARP_PRO_IP		ETHER_TYPE_IP

#define ARP_OP_REQUEST	(1)
#define ARP_OP_REPLY	(2)

#define ARP_CACHE_SIZE	(32)

#define ARP_CACHE_STATE_FREE		(0)		// 未使用
#define ARP_CACHE_STATE_IMCOMPLETE	(1)		// 問い合わせ中
#define ARP_CACHE_STATE_RESOLVED	(2)		// 解決済み
#define ARP_CACHE_STATE_STATIC		(3)		// 静的に登録されたキャッシュ

#define ARP_TIMER_PERIOD			(100)			// 100ms
#define ARP_CACHE_TIMEOUT			(1000*60*20)	// 20分 (*)一般的には20分らしい

// 制御ブロック
typedef struct {
	osMutexId			lock;						// ミューテックス
	osTimerId			timer_id;					// 大麻ID
	struct arp_cache	caches[ARP_CACHE_SIZE];		// ARPキャッシュ
}ARP_CB;
static ARP_CB arp_cb;
#define get_myself() (&arp_cb)

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

// ARPキャッシュを削除
static void arp_cache_delete(struct arp_cache * cache)
{
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	
	debugf("DELETE: pa=%s, ha=%s", ip_addr_ntop(cache->pa, addr1, sizeof(addr1)), ether_drv_addr_ntop(cache->ha, addr2, sizeof(addr2)));
	// 値の設定
	cache->state = ARP_CACHE_STATE_FREE;
	cache->pa = 0;
	memset(cache->ha, 0, ETHER_ADDR_LEN);
	cache->timestamp = 0;
}

// 古くなったキャッシュの削除
void arp_timer(void const *arg)
{
	ARP_CB *this = get_myself();
	struct arp_cache *entry;
	uint32_t now, diff;
	
	osMutexWait(this->lock, osWaitForever);
	now = osKernelSysTick();
	for (entry = this->caches; entry < tailof(this->caches); entry++) {
		if ((entry->state != ARP_CACHE_STATE_FREE) && (entry->state != ARP_CACHE_STATE_STATIC)) {
			diff = now - entry->timestamp;
			if (ARP_CACHE_TIMEOUT < diff) {
				arp_cache_delete(entry);
			}
		}
	}
	osMutexRelease(this->lock);
}

// ARPキャッシュの検索
static struct arp_cache * arp_cache_select(ip_addr_t pa)
{
	ARP_CB *this = get_myself();
	struct arp_cache *entry;
	
	for (entry = this->caches; entry < tailof(this->caches); entry++) {
		if ((entry->state != ARP_CACHE_STATE_FREE) && (entry->pa == pa)) {
			return entry;
		}
	}
	return NULL;
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

// ARP反応
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

// ARPキャッシュの更新
static struct arp_cache * arp_cache_update(ip_addr_t pa, const uint8_t *ha)
{
	struct arp_cache *cache;
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	
	// 空きキャッシュの検索
	cache = arp_cache_select(pa);
	if (cache == NULL) {
		debugf("cache not found");
		return NULL;
	}
	// 値設定
	cache->state = ARP_CACHE_STATE_RESOLVED;
	memcpy(cache->ha, ha, ETHER_ADDR_LEN);
	cache->timestamp = osKernelSysTick();
	debugf("UPDATE: pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)), ether_drv_addr_ntop(ha, addr2, sizeof(addr2)));
	
	return cache;
}

// キャッシュ領域の確保
static struct arp_cache * arp_cache_alloc(void)
{
	ARP_CB *this = get_myself();
	struct arp_cache *entry, *oldest = NULL;
	
	for (entry = this->caches; entry < tailof(this->caches); entry++) {
		if (entry->state == ARP_CACHE_STATE_FREE) {
			return entry;
		}
		// タイムスタンプが最も古いエントリを覚えておく
		//if (!oldest || timercmp(&oldest->timestamp, &entry->timestamp, >)) {
		if (!oldest || (oldest->timestamp > entry->timestamp)) {
			oldest = entry;
		}
	}
	// 空きが見つからない場合は、一番タイムスタンプが古いエントリを削除
	arp_cache_delete(oldest);
	return oldest;
}

// ARPキャッシュの登録
static struct arp_cache * arp_cache_insert(ip_addr_t pa, uint8_t *ha)
{
	struct arp_cache *cache;
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	
	// キャッシュ領域の割り当て
	cache = arp_cache_alloc();
	if (cache == NULL) {
		errorf("arp_cache_alloc failure");
		return NULL;
	}
	// 値の設定
	cache->state = ARP_CACHE_STATE_RESOLVED;
	cache->pa = pa;
	memcpy(cache->ha, ha, ETHER_ADDR_LEN);
	cache->timestamp = osKernelSysTick();
	debugf("INSERT: pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)), ether_drv_addr_ntop(ha, addr2, sizeof(addr2)));
	return cache;
}

// ARPメッセージの入力
static void arp_input(const uint8_t *data, size_t len, struct net_device *dev)
{
	ARP_CB *this = get_myself();
	struct arp_ether_ip *msg;
	ip_addr_t spa, tpa;
	struct net_iface *iface;
	int32_t merge = 0;
	
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
	
	// ARPキャッシュの更新
	// (*) 自分宛かどうか関係なしに更新
	osMutexWait(this->lock, osWaitForever);
	if (arp_cache_update(spa, msg->sha)) {
		// updated
		merge = 1;
	}
	osMutexRelease(this->lock);
	
	// IPインタフェースを取得
	iface = net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
	// 自身宛ての場合は、ARP応答
	if (iface && ((struct ip_iface*)iface)->unicast == tpa) {
		// ARPキャッシュの登録
		if (!merge) {
			osMutexWait(this->lock, osWaitForever);
			arp_cache_insert(spa, msg->sha);
			osMutexRelease(this->lock);
		}
		// ARPリプライ
		if (ntoh16(msg->hdr.op) == ARP_OP_REQUEST) {
			arp_reply(iface, msg->sha, spa);
		}
	}
}

osStatus arp_init(void)
{
	ARP_CB *this = get_myself();
	osStatus ercd;
	
	// 制御ブロック初期化
	memset(this, 0, sizeof(ARP_CB));
	
	// ミューテックス作成
	osMutexDef(myLock);
	this->lock = osMutexCreate(osMutex(myLock));
	if (this->lock == NULL) {
		return osErrorOS;
	}
	
	// 大麻ハンドラ生成
	osTimerDef(MyTimer, arp_timer);
	this->timer_id = osTimerCreate(osTimer(MyTimer), osTimerPeriodic, NULL);
	if (this->timer_id == NULL) {
		return osErrorOS;
	}
	
	// 入力ハンドラの登録
	if ((ercd = net_protocol_register(NET_PROTOCOL_TYPE_ARP, arp_input)) != osOK) {
		errorf("net_protocol_register() failed");
		goto ARP_INIT_END;
	}
	
	// タイマハンドラ開始
	osTimerStart(this->timer_id, ARP_TIMER_PERIOD);
	
ARP_INIT_END:
	return ercd;
}

// アドレス解決
int arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8_t *ha)
{
	ARP_CB *this = get_myself();
	struct arp_cache *cache;
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	
	// ハードウェアアドレスとプロトコルアドレスのチェック
	if (iface->dev->type != NET_DEVICE_TYPE_ETHRNET) {
		debugf("unspported hardware address type");
		return ARP_RESOLVE_ERROR;
	}
	if (iface->family != NET_IFACE_FAMILY_IP) {
		debugf("unspported protocol address type");
		return ARP_RESOLVE_ERROR;
	}
	
	// ARPOキャッシュの検索
	osMutexWait(this->lock, osWaitForever);
	cache = arp_cache_select(pa);
	if (!cache) {
		debugf("cache not found, pa=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)));
		osMutexRelease(this->lock);
		return ARP_RESOLVE_ERROR;
	}
	memcpy(ha, cache->ha, ETHER_ADDR_LEN);
	osMutexRelease(this->lock);
	debugf("resolved, pa=%s, ha=%s", ip_addr_ntop(pa, addr1, sizeof(addr1)), ether_drv_addr_ntop(ha, addr2, sizeof(addr2)));
	
	return ARP_RESOLVE_FOUND;
}
