/*
 * tcp.c
 *
 *  Created on: Nov 14, 2026
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

#include "tcp.h"

/*
本TCPで実装する機能は、
コネクション管理、確認応答、フロー制御、再送制御
*/

#define TCP_DBG_PRINT(fmt, ...) debugf("[TCP ]:"fmt,  ##__VA_ARGS__)
#define TCP_INFO_PRINT(fmt, ...) infof("[TCP ]:"fmt,  ##__VA_ARGS__)
#define TCP_ERR_PRINT(fmt, ...) errorf("[TCP ]:"fmt,  ##__VA_ARGS__)

#define TCP_FLG_FIN		(0x01)
#define TCP_FLG_SYN		(0x02)
#define TCP_FLG_RST		(0x04)
#define TCP_FLG_PSH		(0x08)
#define TCP_FLG_ACK		(0x10)
#define TCP_FLG_URG		(0x20)

#define TCP_FLG_ISSET(x, y) ((x & 0x3F) & (y) ? 1 : 0)

// 疑似ヘッダ
struct pseudo_hdr {
	uint32_t	src;		// 送信元アドレス
	uint32_t	dst;		// 宛先IPアドレス
	uint8_t		zero;		// 未使用のフィールド。常に0
	uint8_t		protocol;	// プロトコル番号
	uint16_t	len;		// TCPセグメントの長さ。
};

struct tcp_hdr {
	uint16_t	src;		// 送信ポート
	uint16_t	dst;		// 受信ポート
	uint32_t	seq;		// シーケンス番号
	uint32_t	ack;		// 確認応答番号
	uint8_t		off;		// データオフセット
	uint8_t		flg;		// コントロールビット
	uint16_t	wnd;		// ウィンドウサイズ
	uint16_t	sum;		// チェックサム
	uint16_t	up;			// オプション
};

// 制御ブロック
typedef struct {
	uint8_t tmp;
} TCP_CB;
static TCP_CB tcp_cb;
#define get_myself() (&tcp_cb)

// 制御フラグの視覚化
static char * tcp_flg_ntoa(uint8_t flg)
{
	static char str[9];
	
	snprintf(str, sizeof(str), "--%c%c%c%c%c%c",
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'U' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'A' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'P' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'R' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'S' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'T' : '-');
	
	return str;
}

static char * tcp_opt_ntoa(uint8_t opt)
{
	switch (opt) {
	case 0:
		return "End of Option List (EOL)";
	case 1:
		return "No-Operation (NOP)";
	case 2:
		return "Maximum Segment Size (MSS)";
	case 3:
		return "Window Scale";
	case 4:
		return "SACK Permitted";
	case 5:
		return "SACK";
	case 8:
		return "Timestamps";
	default:
		return "Unknown";
	}
}

static void tcp_print(const uint8_t *data, size_t len)
{
	struct tcp_hdr *hdr;
	uint8_t hlen, *opt;
	int i = 0;
	
	hdr = (struct tcp_hdr*)data;
	
	TCP_DBG_PRINT("src:%d", ntoh16(hdr->src));
	TCP_DBG_PRINT("dst:%d", ntoh16(hdr->dst));
	TCP_DBG_PRINT("seq:%d", ntoh16(hdr->seq));
	TCP_DBG_PRINT("ack:%d", ntoh16(hdr->ack));
	hlen = (hdr->off >> 4) << 2;
	TCP_DBG_PRINT("off:0x%x (%d) options: %d, payload: %d", hdr->off, hlen, hlen - sizeof(*hdr), len - hlen);
	TCP_DBG_PRINT("flg:0x%x (%s)", hdr->flg, tcp_flg_ntoa(hdr->flg));	
	TCP_DBG_PRINT("wnd:%d", ntoh16(hdr->wnd));
	TCP_DBG_PRINT("sum:%d", ntoh16(hdr->sum));
	TCP_DBG_PRINT("up:%d", ntoh16(hdr->up));
	opt = (uint8_t*)(hdr+1);
	while(opt < (uint8_t*)hdr + hlen) {
		if (*opt == 0) {
			TCP_DBG_PRINT("opt:[%d]: kind=%d (%s)", i++, *opt, tcp_flg_ntoa(*opt));
			break;
		}
		if (*opt == 1) {
			TCP_DBG_PRINT("opt:[%d]: kind=%d (%s)", i++, *opt, tcp_flg_ntoa(*opt));
			opt++;
		} else {
			TCP_DBG_PRINT("opt:[%d]: kind=%d, len=%d", i++, *opt, tcp_flg_ntoa(*opt), *(opt+1));
			opt += *(opt+1);
		}
	}
}

// 入力
static void tcp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
	struct tcp_hdr *hdr;
	struct pseudo_hdr pseudo;
	uint16_t psum;
	ip_endp_t src, dst;
	char ep1[IP_ENDP_STR_LEN];
	char ep2[IP_ENDP_STR_LEN];
	
	// データサイズのチェック
	if (len < sizeof(*hdr)) {
		TCP_ERR_PRINT("too short");
		return;
	}
	hdr = (struct tcp_hdr*)data;
	pseudo.src = iphdr->src;
	pseudo.dst = iphdr->dst;
	pseudo.zero = 0;
	pseudo.protocol = IP_PROTOCOL_TCP;
	pseudo.len = hton16(len);
	// チェックサムの検証
	psum = ~cksum16((uint16_t*)&pseudo, sizeof(pseudo), 0);
	if (cksum16((uint16_t*)hdr, len, psum) != 0) {
		TCP_ERR_PRINT("check sum error");
		return;
	}
	src.addr = iphdr->src;
	src.port = hdr->src;
	dst.addr = iphdr->dst;
	dst.port = hdr->dst;
	ip_endp_ntop(src, ep1, sizeof(ep1));
	ip_endp_ntop(dst, ep1, sizeof(ep2));
	// 送信元アドレスと宛先アドレスの検証
	// TCPはエンドポイント間で行われる1:1のユニキャスト通信であるため、ブロードキュアストはあり得ません
	if ((src.addr == IP_ADDR_BROADCAST) || (src.addr == iface->broadcast) || 
		(dst.addr == IP_ADDR_BROADCAST) || (dst.addr == iface->broadcast)) {
		TCP_ERR_PRINT("only supports unicasr, src=%s. dst=%s", ep1, ep2);
		return;
	}
	TCP_DBG_PRINT("%s=>%s, len=%d, dev=%s", ep1, ep2, len, NET_IFACE(iface)->dev->name);
	tcp_print(data, len);
	return;
}

// 初期化
osStatus tcp_init(void)
{
	TCP_CB *this = get_myself();
	
	// TCPモジュールの登録
	if (ip_protocol_register(IP_PROTOCOL_TCP, tcp_input) != osOK) {
		TCP_ERR_PRINT("ip_protocol_register erorr");
		return osErrorResource;
	}
	return osOK;
}
