/*
 * ip.c
 *
 *  Created on: Jul 6, 2026
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
#include "icmp.h"
#include "arp.h"

#define IP_HDR_OFFSET_MASK		(0x1FFF)
#define IP_HDR_FLAG_MF			(0x2000)	// more flagments flag
#define IP_HDR_FLAG_DF			(0x4000)	// don't flagments flag
#define IP_HDR_FLAG_RF			(0x8000)	// reserved

#define IP_IFACE_NUM			(4)			// インタフェースの最大値
#define IP_PROTOCOL_NUM			(4)			// プロトコル数の最大値

const ip_addr_t IP_ADDR_ANY = 0x00000000;			// 0.0.0.0
const ip_addr_t IP_ADDR_BROADCAST = 0xFFFFFFFF;		// 255.255.255.255

struct ip_protocol {
	struct ip_protocol *next;		// 連結リストの次の要素をさすポインタ
	uint8_t protocol;				// プロトコル番号
	ip_protocol_handler_t handler;	// プロトコルの入力データを処理する関数
};

// 制御ブロック
typedef struct {
	osPoolId			iface_id;						// ネットワークデバイス用のメモリプールID
	osPoolId			protocol_id;					// プロトコル用のメモリプールID
	struct ip_iface 	*ifaces;						// 連結リスト
	struct ip_protocol	*protocols;						// 連結リスト
	uint8_t				send_buf[IP_TOTAL_SIZE_MAX];	// 送信バッファ
}IP_CB;
static IP_CB ip_cb;
#define get_myself() (&ip_cb)

// 文字列からバイナリへ変換
osStatus ip_addr_pton(const char *p, ip_addr_t *n)
{
	char *sp, *ep;
	int32_t idx;
	uint32_t ret;
	
	sp = (char*)p;
	
	for (idx = 0; idx < 4; idx++) {
		// 数値に変換
		ret = strtol(sp, &ep, 10);
		if ((ret < 0) || (ret > 255)) {
			// 例えば192.168.0.1の場合、retには数値としての198、epには".168.0.1"が格納される
			return osErrorValue;
		}
		// 数字が読めなかったらエラー
		if (ep == sp) {
			return osErrorValue;
		}
		if (((idx == 3) && (*ep != '\0')) || ((idx != 3) && (*ep != '.') )) {
			return osErrorValue;
		}
		((uint8_t*)n)[idx] = ret;
		sp = ep + 1;
	}
	
	return osOK;
}

// バイナリから文字列へ変換
char *ip_addr_ntop(ip_addr_t n, char *p, size_t size)
{
	uint8_t *u8;
	
	u8 = (uint8_t *)&n;
	snprintf(p, size, "%d.%d.%d.%d", u8[0], u8[1], u8[2], u8[3]);
	
	return p;
}

// IPパケットの詳細出力
static void ip_print(const uint8_t *data, size_t len)
{
	struct ip_hdr *hdr;
	uint8_t v, hl, hlen;
	uint16_t total, offset;
	char addr[IP_ADDR_STR_LEN];
	
	// 値抽出
	hdr = (struct ip_hdr *)data;
	v = (hdr->vhl >> 4);
	hl = (hdr->vhl & 0x0f);
	hlen = (hl << 2);
	total = ntoh16(hdr->total);
	offset = ntoh16(hdr->offset);
	
	// 表示
	debugf("v=%d, hl=%d (%d)", v, hl, hlen);
	debugf("tos=%d", hdr->tos);
	debugf("total=%d payload=%d)", total, total - hlen);
	debugf("id=%d", ntoh16(hdr->id));
	debugf("flags=%x, offset=%d", (offset >> 13), offset & IP_HDR_OFFSET_MASK);
	debugf("ttl=%d", hdr->ttl);
	debugf("protocol=%d", hdr->protocol);
	debugf("sum=%x", ntoh16(hdr->sum));
	debugf("src=%s", ip_addr_ntop(hdr->src, addr, sizeof(addr)));
	debugf("dst=%s", ip_addr_ntop(hdr->dst, addr, sizeof(addr)));
}

// 入力ハンドラ
static void ip_input(const uint8_t *data, size_t len, struct net_device *dev)
{
	IP_CB *this = get_myself();
	struct ip_hdr *hdr;
	uint8_t v;
	uint16_t hlen, total, offset;
	struct ip_iface *iface;
	char addr[IP_ADDR_STR_LEN];
	struct ip_protocol *proto;
	
	debugf("dev=%s, len=%d", dev->name, len);
	HEXDUMP(data, len);
	
	// IPヘッダの最小サイズは20なので、それ以下の場合はエラー
	if (len < IP_HDR_SIZE_MIN) {
		errorf("too short");
		return;
	}
	// バージョンチェック
	hdr = (struct ip_hdr *)data;
	v = (hdr->vhl >> 4);
	if (v != IP_VERSION_IPV4) {
		errorf("    ip version error:v=%d", v);
		return;
	}
	// ヘッダ長のチェック
	hlen = ((hdr->vhl & 0x0F) << 2);
	if (len < hlen) {
		errorf("    header length error:len=%d<hlen=%d", len, hlen);
		return;
	}
	// チェックサム検証
	if (cksum16((uint16_t*)hdr, hlen, 0) != 0) {
		errorf("    chechsum error");
		return;
	}
	// IPパケット長チェック
	total = ntoh16(hdr->total);
	if (len < total) {
		errorf("    total length error:len=%d<total=%d", len, total);
		return;
	}
	// フラグメンテーションのチェック (*) IPでのパケット分割はサポートしない
	offset = ntoh16(hdr->offset);
	if (((offset & IP_HDR_FLAG_MF) != 0)||((offset & IP_HDR_OFFSET_MASK) != 0)) {
		errorf(" flagment dpes mpt support");
		return;
	}
	// IPv4のインタフェースを取得
	iface = (struct ip_iface *)net_device_get_iface(dev, NET_IFACE_FAMILY_IP);
	if (iface == NULL) {
		// ignore
		return;
	}
	// 自分宛じゃない
	if (hdr->dst != iface->unicast) {
		// ブロードキャストでもない
		if (hdr->dst != iface->broadcast && hdr->dst != IP_ADDR_BROADCAST) {
			// ignore fore other host
			return;
		}
	}
	// 表示
//	debugf("permit, dev=%s, iface=%s", dev->name, ip_addr_ntop(iface->unicast, addr, sizeof(addr)));
	ip_print(data, total);
	// プロトコルに対応した入力ハンドラを起動
	for (proto = this->protocols; proto; proto = proto->next) {
		if (proto->protocol == hdr->protocol) {
			proto->handler(hdr, data + hlen, total - hlen, iface);
			return;
		}
	}
	// unsupported protocol
	if ((hlen + 8) <= total) {
		// It should not be sent in responce to ICMP error messages,
		// but ICMP is always registered and will not reach this point.
		// Destination Unreachable メッセージでは、原因となったIPパケットのIPヘッダ+8byteのペイロードをデータ部分にコピーして送信する
		// この8byteの中には、上位プロトコルの情報だったりが入っている
		icmp_output(ICMP_TYPE_DEST_UNREACH, ICMP_CODE_PROTO_UNREACH, 0, data, hlen + 8, iface->unicast, hdr->src);
	}
}

// 初期化
osStatus ip_init(void)
{
	IP_CB *this = get_myself();
	
	// 制御ブロック初期化
	memset(this, 0, sizeof(IP_CB));
	// インタフェース用のメモリプール確保
	osPoolDef(MemPool_1, IP_IFACE_NUM, struct ip_iface);
	this->iface_id = osPoolCreate (osPool (MemPool_1));
	if (this->iface_id == NULL) {
		return osErrorOS;
	}
	// ネットワークプロトコル用のメモリプール確保
	osPoolDef(MemPool_2, IP_PROTOCOL_NUM, struct ip_protocol);
	this->protocol_id = osPoolCreate (osPool (MemPool_2));
	if (this->protocol_id == NULL) {
		return osErrorOS;
	}
	// プロトコル登録
	if (net_protocol_register(NET_PROTOCOL_TYPE_IP, ip_input) != osOK) {
		errorf("net_protocol_register failure");
		return osErrorResource;
	}
	return osOK;
}

// IPインタフェースの確保
struct ip_iface *ip_iface_alloc(const char *unicast, const char *netmask)
{
	IP_CB *this = get_myself();
	struct ip_iface *iface;
	
	// インタフェース用のメモリを確保
	iface = osPoolCAlloc(this->iface_id);
	if (iface == NULL) {
		errorf("osPoolCAlloc() failure");
		return NULL;
	}
	// ファミリの設定
	NET_IFACE(iface)->family = NET_IFACE_FAMILY_IP;
	// ユニキャストIPアドレスの設定
	if (ip_addr_pton(unicast, &iface->unicast) != osOK) {
		errorf("ip_addr_pton failure, addr=%s", unicast);
		osPoolFree(this->iface_id, iface);
		return NULL;
	}
	// サブネットマスクの設定
	if (ip_addr_pton(netmask, &iface->netmask) != osOK) {
		errorf("ip_addr_pton failure, addr=%s", netmask);
		osPoolFree(this->iface_id, iface);
		return NULL;
	}
	// ブロードキャストアドレスの設定
	// サブネットワーク内の全ノードへ一斉送信のためのアドレス
	iface->broadcast = (iface->unicast & iface->netmask) | ~iface->netmask;
	
	return iface;
}

// ネットワークデバイスのインタフェースを紐づけ
osStatus ip_iface_register(struct net_device *dev, struct ip_iface *iface)
{
	IP_CB *this = get_myself();
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	char addr3[IP_ADDR_STR_LEN];
	
	infof("dev=%s, %s, %s, %s", dev->name, ip_addr_ntop(iface->unicast, addr1, sizeof(addr1)), ip_addr_ntop(iface->netmask, addr2, sizeof(addr2)), ip_addr_ntop(iface->broadcast, addr3, sizeof(addr3)));
	// インタフェース登録
	if (net_device_add_iface(dev, NET_IFACE(iface)) != osOK) {
		errorf("net_device_add_iface failure");
		return osErrorResource;
	}
	iface->next = iface;
	this->ifaces = iface;
	
	return osOK;
}

// IPインタフェースの検索
struct ip_iface * ip_iface_select(ip_addr_t addr)
{
	IP_CB *this = get_myself();
	struct ip_iface *entry;
	
	// 検索
	for (entry = this->ifaces; entry; entry = entry->next) {
		if (entry->unicast == addr) {
			break;
		}
	}
	return entry;
}

// IPパケットの組み立て
static ssize_t ip_build_packet(uint8_t protocol, const uint8_t *data, size_t len, uint16_t id, uint16_t offset, ip_addr_t src, ip_addr_t dst, uint8_t *buf, size_t size)
{
	uint16_t hlen, total;
	struct ip_hdr *hdr;
	
	hlen = IP_HDR_SIZE_MIN;
	total = hlen + len;
	if (size < total) {
		return osErrorResource;
	}
	// IPヘッダのフィールドに値を設定
	hdr = (struct ip_hdr*)buf;
	hdr->vhl = ((IP_VERSION_IPV4 << 4) | (hlen >> 2));
	hdr->tos = 0;
	hdr->total = hton16(total);
	hdr->id = hton16(id);
	hdr->offset = hton16(offset);
	hdr->ttl = 0xff;
	hdr->protocol = hton16(protocol);
	hdr->sum = 0;
	hdr->src = src;
	hdr->dst = dst;
	hdr->sum = cksum16((uint16_t*)hdr, hlen, 0);
	memcpy(buf+hlen, data, len);
	ip_print(buf, total);
	
	return (ssize_t)total;
}

// ネットワークデバイスからの送信
static int ip_output_device(struct ip_iface *iface, const uint8_t *data, size_t len, ip_addr_t target)
{
	char addr[IP_ADDR_LEN];
	uint8_t hwaddr[NET_DEVICE_ADDR_LEN] = {};
	int ret;
	
	ip_addr_ntop(target, addr, sizeof(addr));
	debugf("dev=%s, len=%d, target=%s", NET_IFACE(iface)->dev->name, len, addr);
	// アドレス解決が必要な場合
	if ((NET_IFACE(iface)->dev->flags & NET_DEVICE_FLAG_NEED_ARP) != 0) {
		// ブロードキャストIPアドレスの場合
		if ((target == iface->broadcast) || (target == IP_ADDR_BROADCAST)) {
			memcpy(hwaddr, NET_IFACE(iface)->dev->bloadcast, NET_IFACE(iface)->dev->alen);
		} else {
			// ARP解決
			ret = arp_resolve(NET_IFACE(iface), target, hwaddr);
			if (ret != ARP_RESOLVE_FOUND) {
				return osErrorResource;
			}
		}
	}
	return net_device_output(NET_IFACE(iface)->dev, NET_PROTOCOL_TYPE_IP, data, len, hwaddr);
}

// IPパケットをネットワークデバイスから送信する関数
ssize_t ip_output(uint8_t protocol, const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst)
{
	IP_CB *this = get_myself();
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	struct ip_iface *iface;
	uint16_t id;
	ssize_t plen;
	uint8_t *buf = this->send_buf;
	
	ip_addr_ntop(src, addr1, sizeof(addr1));
	ip_addr_ntop(dst, addr2, sizeof(addr2));
	debugf("%s=>%s, protocol=%d, len=%d", addr1, addr2, protocol, len);
	// 送信元アドレスの検証
	// ANYはIPアドレスが指定されていないからの状態を表している。
	// 送信元が明示的に指定されない場合、いったんエラー（本来はルーティングテーブルを参照して宛先に到達可能なインタフェースのIPを自動的に選択する）
	if(src == IP_ADDR_ANY) {
		errorf("ip routing does not implement");
		return osErrorResource;
	}
	// 出力先インタフェースの決定
	iface = ip_iface_select(src);
	if (iface == NULL) {
		errorf("iiface not found, src=%s", addr1);
		return osErrorResource;
	}
	// 宛先アドレスの検証
	// 
	if ((dst & iface->netmask) != (iface->unicast & iface->netmask) && (dst != IP_ADDR_BROADCAST)) {
		errorf("not reached, dst=%s", addr2);
		return osErrorResource;
	}
	if (NET_IFACE(iface)->dev->mtu < IP_HDR_SIZE_MIN + len) {
		errorf("too long, dev=%s, mtu=%d<%d", NET_IFACE(iface)->dev->name, NET_IFACE(iface)->dev->mtu, IP_HDR_SIZE_MIN + len);
		return osErrorResource;
	}
	id = random();
	plen = ip_build_packet(protocol, data, len, id, 0, iface->unicast, dst, buf, sizeof(buf));
	if (plen == -1) {
		errorf("ip_build_packet failure");
		return osErrorResource;
	}
	if (ip_output_device(iface, buf, plen, dst) == -1) {
		errorf("ip_output_device failure");
		return osErrorResource;
	}
	return plen;
}

// プロトコルの登録
osStatus ip_protocol_register(uint8_t protocol, ip_protocol_handler_t handler)
{
	IP_CB *this = get_myself();
	struct ip_protocol *entry;
	
	// 重複チェック
	for (entry = this->protocols; entry; entry = entry->next) {
		if (entry->protocol == protocol) {
			errorf("already exists, protocol=%d", protocol);
			return osErrorResource;
		}
	}
	// メモリ確保
	entry = osPoolCAlloc(this->protocol_id);
	if (entry == NULL) {
		errorf("osPoolCAlloc() failure");
		return osErrorResource;
	}
	entry->protocol = protocol;
	entry->handler = handler;
	entry->next = this->protocols;
	this->protocols = entry;
	infof("success, protocol=%u", protocol);
	
	return osOK;
}
