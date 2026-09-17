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

#define UDP_DBG_PRINT(fmt, ...) debugf("[UDP]:"fmt,  ##__VA_ARGS__)

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

// UDPデータグラムの入力
static void udp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
	struct udp_hdr *hdr;
	uint16_t total;
	struct pseudo_hdr pseudo;
	uint16_t psum = 0;
	ip_endp_t src, dst;
	char endp1[IP_ENDP_STR_LEN];
	char endp2[IP_ENDP_STR_LEN];
	
	// データサイズの検証
	if (len < sizeof(*hdr)) {
		errorf("too short");
		return;
	}
	hdr = (struct udp_hdr *)data;
	total = ntoh16(hdr->len);
	if (len < total) {
		errorf("length error:len=%d, hdr->len=%d", len, total);
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
			errorf("checksum erorr");
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
}

// 初期化
osStatus udp_init(void)
{
	// UDPモジュールの登録
	if (ip_protocol_register(IP_PROTOCOL_UDP, udp_input) != osOK) {
		errorf("ip_protocol_register erorr");
		return osErrorResource;
	}
	return osOK;
}
