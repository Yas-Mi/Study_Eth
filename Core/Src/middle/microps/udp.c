/*
 * udp.c
 *
 *  Created on: Sep 16, 2026
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

#define UDP_DBG_PRINT(fmt, ...) debugf("[UDP ]:"fmt,  ##__VA_ARGS__)
#define UDP_INFO_PRINT(fmt, ...) infof("[UDP ]:"fmt,  ##__VA_ARGS__)
#define UDP_ERR_PRINT(fmt, ...) errorf("[UDP ]:"fmt,  ##__VA_ARGS__)
#define UDP_PCB_STATE_FREE			(0)		// 未使用
#define UDP_PCB_STATE_OPEN			(1)		// 使用中
#define UDP_PCB_STATE_CLOSING		(2)		// 解放処理中
#define UDP_PCB_SIZE				(16)
#define UDP_PACKET_NUM				(4)		// UDPパケット最大保持数

// UDPヘッダ
struct udp_hdr {
	uint16_t src;
	uint16_t dst;
	uint16_t len;
	uint16_t sum;
};

// 疑似ヘッダ
struct pseudo_hdr {
	uint32_t src;		// 送信元IPアドレス
	uint32_t dst;		// 宛先IPアドレス
	uint8_t zero;		// 未使用のフィールド
	uint8_t protocol;	// プロトコル番号
	uint16_t len;		// データグラムの長さ
};

// UDP制御ブロック構造体
struct udp_pcb {
	int state;			// 状態
	ip_endp_t	local;	// ローカル側のエンドポイント
	struct queue queue;	// 受信キュー
};

// キューエントリ構造体
struct udp_queue_entry {
	struct queue_entry entry;
	ip_endp_t remote;
	uint16_t len;
	uint8_t data[1472];		// フラグメント禁止なので最大は1472
};

// 制御ブロック
typedef struct {
	osMutexId		lock;					// ミューテックス
	osPoolId		udp_pkt_id;				// ネットワークプロトコル用のメモリプールI
	struct udp_pcb	pcbs[UDP_PCB_SIZE];
}UDP_CB;
static UDP_CB udp_cb;
#define get_myself() (&udp_cb)

// UDPデータグラムの詳細出力
static void udp_print(const uint8_t *data, size_t len)
{
	struct udp_hdr *hdr;
	uint16_t total;
	
	hdr = (struct udp_hdr*)data;
	// 表示
	UDP_DBG_PRINT("src:%d", ntoh16(hdr->src));
	UDP_DBG_PRINT("dst:%d", ntoh16(hdr->dst));
	total = ntoh16(hdr->len);
	UDP_DBG_PRINT("len:%d(payload:%d)", total, total - (uint16_t)sizeof(*hdr));
	UDP_DBG_PRINT("sum:%d", ntoh16(hdr->sum));
}

// 制御ブロックの検索
static struct udp_pcb* udp_pcb_select(ip_endp_t key)
{
	UDP_CB *this = get_myself();
	struct udp_pcb* pcb;
	
	for (pcb = this->pcbs; pcb < tailof(this->pcbs); pcb++) {
		if (pcb->state == UDP_PCB_STATE_OPEN) {
			if (pcb->local.port == key.port) {
				if ((pcb->local.addr == key.addr) || (pcb->local.addr == IP_ADDR_ANY) || (key.addr == IP_ADDR_ANY)) {
					return pcb;
				}
			}
		}
	}
	return NULL;
}

// UDPデータグラムの入力
static void udp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
	UDP_CB *this = get_myself();
	struct udp_hdr *hdr;
	uint16_t total;
	struct pseudo_hdr pseudo;
	uint16_t psum = 0;
	ip_endp_t src, dst;
	char endp1[IP_ENDP_STR_LEN];
	char endp2[IP_ENDP_STR_LEN];
	struct udp_pcb* pcb;
	uint16_t iphdrlen;
	struct udp_queue_entry *entry;
	
	// データサイズの検証
	if (len < sizeof(*hdr)) {
		UDP_ERR_PRINT("too short");
		return;
	}
	hdr = (struct udp_hdr *)data;
	total = ntoh16(hdr->len);
	if (len < total) {
		UDP_ERR_PRINT("length error:len=%d, hdr->len=%d", len, total);
		return;
	}
	// チェックサムの検証 (*) チェックサムが0だった場合はチェックサムの検証をスキップ
	if (hdr->sum) {
		// UDPヘッダの前に疑似ヘッダが存在するものとして計算
		pseudo.src = iphdr->src;
		pseudo.dst = iphdr->dst;
		pseudo.zero = 0;
		pseudo.protocol = IP_PROTOCOL_UDP;
		pseudo.len = hton16(total);
		// いったん疑似ヘッダ分のチェックサムを計算
		psum = ~cksum16((uint16_t*)&pseudo, sizeof(pseudo), 0);
		// 続けて、UDPデータグラム本体のチェックサム計算 (*) 疑似ヘッダ分のチェックサムを渡すことで途中から続けられる
		if (cksum16((uint16_t*)hdr, len, psum) != osOK) {
			UDP_ERR_PRINT("checksum erorr");
			return;
		}
	}
	// エンドポイントの情報とUDPデータグラムの詳細を出力
	src.addr = iphdr->src;
	src.port = hdr->src;
	dst.addr = iphdr->dst;
	dst.port = hdr->dst;
	UDP_DBG_PRINT("%s=>%s, len=%d, dev=%s", ip_endp_ntop(src, endp1, sizeof(endp1)), ip_endp_ntop(dst, endp2, sizeof(endp2)), len, NET_IFACE(iface)->dev->name);
	udp_print(data, len);
	osMutexWait(this->lock, osWaitForever);
	pcb = udp_pcb_select(dst);
	if (pcb == NULL) {
		osMutexRelease(this->lock);
		iphdrlen = (iphdr->vhl & 0x0f) << 4;
		icmp_output(ICMP_TYPE_DEST_UNREACH, ICMP_CODE_PORT_UNREACH, 0, (uint8_t*)iphdr, iphdrlen+8, iface->unicast, iphdr->src);
		return;
	}
	// メモリ確保
	entry = osPoolCAlloc(this->udp_pkt_id);
	if (entry == NULL) {
		osMutexRelease(this->lock);
		UDP_ERR_PRINT("memory_alloc error");
		return;
	}
	entry->remote = src;
	entry->len = len = sizeof(*hdr);
	memcpy(entry+1, hdr+1, entry->len);
	if (!queue_push(&pcb->queue, (struct queue_entry*)entry)) {
		osMutexRelease(this->lock);
		UDP_ERR_PRINT("queue_push error");
		return;
	}
	osMutexRelease(this->lock);
}

// 制御ブロックの割り当て
static struct udp_pcb* udp_pcb_alloc(void)
{
	UDP_CB *this = get_myself();
	struct udp_pcb * pcb;
	
	// 割り当て可能なブロックを検索
	for (pcb = this->pcbs; pcb < tailof(this->pcbs); pcb++) {
		if (pcb->state == UDP_PCB_STATE_FREE) {
			pcb->state = UDP_PCB_STATE_OPEN;
			return pcb;
		}
	}
	return NULL;
}

// 制御ブロックの開放
static void udp_pcb_release(struct udp_pcb * pcb)
{
	UDP_CB *this = get_myself();
	struct queue_entry *entry;
	
	pcb->state = UDP_PCB_STATE_FREE;
	pcb->local.addr = IP_ADDR_ANY;
	pcb->local.port = 0;
	while(1) {
		entry = queue_pop(&pcb->queue);
		if (entry == NULL) {
			break;
		}
		UDP_DBG_PRINT("free_queue entry");
		osPoolFree(this->udp_pkt_id, entry);
	}
}

// 制御ブロックのポインタを記述子へ変換
static int32_t udp_pcb_desc(struct udp_pcb * pcb)
{
	UDP_CB *this = get_myself();
	
	return indexof(this->pcbs, pcb);
}

// 制御ブロックの記述子をポインタへ変換
static struct udp_pcb* udp_pcb_get(int desc)
{
	UDP_CB *this = get_myself();
	struct udp_pcb* pcb;
	
	// 範囲外
	if ((desc < 0) || (countof(this->pcbs) <= (size_t)desc)) {
		return NULL;
	}
	pcb = &this->pcbs[desc];
	// 使用中でない場合は、無効な記述子が指定されたものとしてエラーを返す
	if (pcb->state != UDP_PCB_STATE_OPEN) {
		return NULL;
	}
	return pcb;
}

// 初期化
osStatus udp_init(void)
{
	UDP_CB *this = get_myself();
	
	// ミューテックス作成
	osMutexDef(myLock);
	this->lock = osMutexCreate(osMutex(myLock));
	if (this->lock == NULL) {
		return osErrorOS;
	}
	// ネットワークプロトコル用のメモリプール確保
	osPoolDef(MemPool_1, UDP_PACKET_NUM, struct udp_queue_entry);
	this->udp_pkt_id = osPoolCreate (osPool (MemPool_1));
	if (this->udp_pkt_id== NULL) {
		return osErrorOS;
	}
	// UDPモジュールの登録
	if (ip_protocol_register(IP_PROTOCOL_UDP, udp_input) != osOK) {
		UDP_ERR_PRINT("ip_protocol_register erorr");
		return osErrorResource;
	}
	return osOK;
}

// OPEN
int32_t udp_cmd_open(void)
{
	UDP_CB *this = get_myself();
	struct udp_pcb* pcb;
	int32_t desc;
	
	osMutexWait(this->lock, osWaitForever);
	// 利用可能な制御ブロックを割り当てる
	pcb = udp_pcb_alloc();
	if (pcb == NULL) {
		osMutexRelease(this->lock);
		UDP_ERR_PRINT("udp_pcb_alloc error");
		return osErrorResource;
	}
	desc = udp_pcb_desc(pcb);
	osMutexRelease(this->lock);
	UDP_DBG_PRINT("desc=%d", desc);
	return desc;
}

// CLOSE
osStatus udp_cmd_close(int32_t desc)
{
	UDP_CB *this = get_myself();
	struct udp_pcb* pcb;
	
	osMutexWait(this->lock, osWaitForever);
	pcb = udp_pcb_get(desc);
	if (pcb == NULL) {
		osMutexRelease(this->lock);
		UDP_ERR_PRINT("pcb not found, desc=%d", desc);
		return osErrorResource;
	}
	UDP_DBG_PRINT("desc=%d", desc);
	udp_pcb_release(pcb);
	osMutexRelease(this->lock);
	
	return osOK;
}

// BIND
osStatus udp_cmd_bind(int32_t desc, ip_endp_t local)
{
	UDP_CB *this = get_myself();
	struct udp_pcb* pcb, *exist;
	char endp1[IP_ENDP_STR_LEN];
	char endp2[IP_ENDP_STR_LEN];
	
	osMutexWait(this->lock, osWaitForever);
	pcb = udp_pcb_get(desc);
	if (pcb == NULL) {
		osMutexRelease(this->lock);
		UDP_ERR_PRINT("pcb not found, desc=%d", desc);
		return osErrorResource;
	}
	// エンドポイントの衝突チェック
	exist = udp_pcb_select(local);
	if (exist != NULL) {
		osMutexRelease(this->lock);
		UDP_ERR_PRINT("already in use, desc=%d, want=%s, exist=%s", desc, ip_endp_ntop(local, endp1, sizeof(endp1)), ip_endp_ntop(exist->local, endp2, sizeof(endp2)));
		return osErrorResource;
	}
	pcb->local = local;
	UDP_DBG_PRINT("desc=%d, %s", desc, ip_endp_ntop(pcb->local, endp1, sizeof(endp1)));
	osMutexRelease(this->lock);
	
	return osOK;
}
