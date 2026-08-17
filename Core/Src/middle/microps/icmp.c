/*
 * icmp.c
 *
 *  Created on: Jul 21, 2026
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

#define ICMP_BUFSIZ IP_PAYLOAD_SIZE_MAX

#define	icmp_type	com.type
#define	icmp_code	com.code
#define	icmp_sum	com.sum

struct icmp_common {
	uint8_t	type;
	uint8_t code;
	uint16_t sum;
};

struct icmp_hdr {
	struct icmp_common com;	// 共通のヘッダーフィールド
	uint32_t dep;			//  message dependent field
};

struct icmp_echo {
	struct icmp_common com;	// 共通のヘッダーフィールド
	uint16_t id;
	uint16_t seq;
};

struct icmp_dest_unreach {
	struct icmp_common com;	// 共通のヘッダーフィールド
	uint32_t unused;
};

// メッセージタイプを文字列に変換
static char * icmp_type_ntoa(uint8_t type)
{
	switch (type) {
	case 	ICMP_TYPE_ECHO_REPLY:
		return "EchoReply";
	case 	ICMP_TYPE_DEST_UNREACH:
		return "DestinationUnreachable";
	case 	ICMP_TYPE_SOURCE_QUENCH:
		return "SourceQuench";
	case 	ICMP_TYPE_REDIRECT:
		return "Redirect";
	case 	ICMP_TYPE_ECHO:
		return "Echo";
	case 	ICMP_TYPE_TIME_EXCEEDED:
		return "TimeExceeded";
	case 	ICMP_TYPE_PARAM_PROBLEM:
		return "ParameterProblem";
	case 	ICMP_TYPE_TIMESTAMP:
		return "Timestamp";
	case 	ICMP_TYPE_TIMESTAMP_REPLY:
		return "TimestampReply";
	case 	ICMP_TYPE_INFO_REQUEST:
		return "InfomationRequest";
	case 	ICMP_TYPE_INFO_REPLY:
		return "InfomationReply";
	}
	return "Unknown";
}

// ICMPの詳細出力
static void icmp_print(const uint8_t *data, size_t len)
{
	struct icmp_hdr *hdr;
	struct icmp_echo *echo;
	struct icmp_dest_unreach *unreach;
	
	hdr = (struct icmp_hdr*)data;
	debugf("  type:%d(%s)", hdr->icmp_type, icmp_type_ntoa(hdr->icmp_type));
	debugf("  code:%d", hdr->icmp_code);
	debugf("  sum:%x", ntoh16(hdr->icmp_sum));
	
	switch (hdr->icmp_type) {
	case ICMP_TYPE_ECHO_REPLY:
	case ICMP_TYPE_ECHO:
		echo = (struct icmp_echo*)hdr;
		debugf("    id:%d", ntoh16(echo->id));
		debugf("    seq:%d", ntoh16(echo->seq));
		break;
	case ICMP_TYPE_DEST_UNREACH:
		unreach = (struct icmp_dest_unreach*)hdr;
		debugf("    unused:%d", ntoh32(unreach->unused));
		break;
	default:
		debugf("    dep:%x", ntoh32(hdr->dep));
		break;
	}
}

// icmpのメッセージ
osStatus icmp_output(uint8_t type, uint8_t code, uint32_t val, const uint8_t *data, size_t len, ip_addr_t src, ip_addr_t dst)
{
	uint8_t buf[ICMP_BUFSIZ];
	struct icmp_hdr *hdr;
	size_t msg_len;
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	
	// 長すぎる
	if (sizeof(buf) < sizeof(*hdr) + len) {
		errorf("too large");
		return osErrorResource;
	}
	// 各フィールドに値を設定
	hdr = (struct icmp_hdr*)buf;
	hdr->icmp_type = type;
	hdr->icmp_code = code;
	hdr->icmp_sum = 0;
	hdr->dep = val;
	memcpy(hdr+1, data, len);
	msg_len = sizeof(*hdr) + len;
	hdr->icmp_sum = cksum16((uint16_t*)hdr, msg_len, 0);
	debugf("%s->%s, len=%d", ip_addr_ntop(src, addr1, sizeof(addr1)), ip_addr_ntop(dst, addr2, sizeof(addr2)), len);
	icmp_print(buf, msg_len);
	return ip_output(IP_PROTOCOL_ICMP, buf, msg_len, src, dst);
}

// icmp入力ハンドラ
static void icmp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
	struct icmp_hdr *hdr;
	char addr1[IP_ADDR_STR_LEN];
	char addr2[IP_ADDR_STR_LEN];
	
	// 短すぎる
	if (len < sizeof(sizeof(*hdr))) {
		errorf("too short");
		return;
	}
	// チェックサム検証
	if (cksum16((uint16_t*)data, len, 0) != 0) {
		errorf("checksum error");
		return;
	}
	debugf("%s->%s, len=%d", ip_addr_ntop(iphdr->src, addr1, sizeof(addr1)), ip_addr_ntop(iphdr->dst, addr2, sizeof(addr2)), len);
	icmp_print(data, len);
	hdr = (struct icmp_hdr*)data;
	switch (hdr->icmp_type) {
	case ICMP_TYPE_ECHO:
		// echo reply 仕様
		//  受信メッセージをそのまま送り返す
		// 宛先としてiphdr->dstではなく、インタフェースのIPアドレス(iface->unicast)を使うのは、ブロードキャストIPアドレスの可能性があるから
		icmp_output(ICMP_TYPE_ECHO_REPLY, hdr->icmp_code, hdr->dep, (uint8_t*)(hdr + 1), len - sizeof(*hdr), iface->unicast, iphdr->src);
		break;
	default:
		// ignore
		break;
	}
}

// 初期化
osStatus icmp_init(void)
{
	// ICMPの入力ハンドラを登録
	if (ip_protocol_register(IP_PROTOCOL_ICMP, icmp_input) != osOK) {
		errorf("ip_protocol_register failure");
		return osErrorResource;
	}
	return osOK;
}
