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

#define TCP_FLG_FIN				(0x01)		// 送信側がも送るデータがないことを示す
#define TCP_FLG_SYN				(0x02)		// 接続開始
#define TCP_FLG_RST				(0x04)		// 接続を即時に破棄する強制終了
#define TCP_FLG_PSH				(0x08)		// バッファが埋まるのを待たずにアプリへ即時渡す要求
#define TCP_FLG_ACK				(0x10)		// ACK番号が有効であることを示す
#define TCP_FLG_URG				(0x20)		// 緊急ポインタが有効であることを示す
#define TCP_FLG_ISSET(x, y)		((x & 0x3F) & (y) ? 1 : 0)

#define TCP_STATE_NONE			(0)		// 状態なし
#define TCP_STATE_CLOSED		(1)		// 接続なし。TCP の開始点・終了点。
#define TCP_STATE_LISTEN		(2)		// サーバが接続要求（SYN）を待っている状態。
#define TCP_STATE_SYN_SENT		(3)		// クライアントが SYN を送信し、SYN+ACK を待っている状態。
#define TCP_STATE_SYN_RECEIVED	(4)		// サーバが SYN を受信し、SYN+ACK を返し、ACK を待っている状態。
#define TCP_STATE_ESTABLISHED	(5)		// 接続確立。双方向にデータ送受信可能。
#define TCP_STATE_FIN_WAIT1		(6)		// 能動クローズ側が FIN を送った直後。ACK または FIN を待つ。
#define TCP_STATE_FIN_WAIT2		(7)		// FIN に対する ACK を受け取り、相手の FIN を待っている状態。
#define TCP_STATE_CLOSE_WAIT	(8)		// 受動クローズ側が FIN を受信し、アプリの close() を待っている状態。
#define TCP_STATE_CLOSING		(9)		// 両端が同時に FIN を送った場合の特殊状態（同時クローズ）。
#define TCP_STATE_LAST_ACK		(10)	// 受動クローズ側が FIN を送信し、ACK を待っている状態。
#define TCP_STATE_TIME_WAIT		(11)	// 能動クローズ側が最後の ACK を送った後、2×MSL 待機する状態。

#define TCP_PCB_SIZE			(4)

// 疑似ヘッダ
struct pseudo_hdr {
	uint32_t	src;		// 送信元アドレス
	uint32_t	dst;		// 宛先IPアドレス
	uint8_t		zero;		// 未使用のフィールド。常に0
	uint8_t		protocol;	// プロトコル番号
	uint16_t	len;		// TCPセグメントの長さ。
};

// 送信シーケンスの構造体
struct snd_vars {
	uint32_t	nxt;
	uint32_t	una;
	uint16_t	wnd;
	uint16_t	up;
	uint32_t	wl1;
	uint32_t	wl2;
};

// 受信シーケンスの構造体
struct rcv_vars {
	uint32_t	nxt;
	uint16_t	wnd;
	uint16_t	up;
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

struct seg_info {
	uint32_t seq;			// シーケンス番号
	uint32_t ack;			// 確認応答番号
	uint16_t len;			// シーケンス番号の消費数
	uint16_t wnd;			// ウィンドウサイズ
	uint16_t up;			// 緊急ポインタ	(*)本書では使用しない
};

struct tcp_pcb {
	int32_t			state;					// コネクション状態
	ip_endp_t		local;					// ローカルエンドポイント
	ip_endp_t		remote;					// リモートエンドポイント
	struct snd_vars	snd;					// 送信シーケンス変数
	uint32_t		iss;					// 初期送信シーケンス番号
	struct rcv_vars	rcv;					// 受信シーケンス変数
	uint32_t		irs;					// 初期受信シーケンス番号
	uint16_t		mss;					// MSS(MaxSegmentSize)の値
	uint8_t			buf[65535];				// receive bffer
	osThreadId		thread_id;				// タスクID
};

// 制御ブロック
typedef struct {
	osMutexId		lock;					// ミューテックス
	struct	tcp_pcb	pcbs[TCP_PCB_SIZE];		// TCP制御ブロック
} TCP_CB;
static TCP_CB tcp_cb;
#define get_myself() (&tcp_cb)

// 制御フラグの視覚化
static char * tcp_flg_ntoa(uint8_t flg)
{
	static char str[9];
	
	snprintf(str, sizeof(str), "--%c%c%c%c%c%c",
		TCP_FLG_ISSET(flg, TCP_FLG_URG) ? 'U' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_ACK) ? 'A' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_PSH) ? 'P' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_RST) ? 'R' : '-',
		TCP_FLG_ISSET(flg, TCP_FLG_SYN) ? 'S' : '-',
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

// ポインタkら奇術師に変換
static int32_t tcp_pcb_desc(struct tcp_pcb *pcb)
{
	TCP_CB *this = get_myself();
	
	return indexof(this->pcbs, pcb);
}

// 記述子からポインタに変換
static struct tcp_pcb * tcp_pcb_get(int32_t desc)
{
	TCP_CB *this = get_myself();
	struct tcp_pcb *pcb;
	
	// 範囲チェック
	if ((desc < 0) || ((int32_t)countof(this->pcbs) <= desc)) {
		return NULL;
	}
	pcb = &(this->pcbs[desc]);
	if (pcb->state == TCP_STATE_NONE) {
		return NULL;
	}
	return pcb;
}

// TCP制御ブロックの検索
static struct tcp_pcb * tcp_pcb_select(ip_endp_t key1, ip_endp_t key2)
{
	TCP_CB *this = get_myself();
	struct tcp_pcb *pcb, *candidate = NULL;
	
	for (pcb = this->pcbs; pcb < tailof(this->pcbs); pcb++) {
		if (pcb->local.port != key1.port) {
			continue;
		}
		// ローカルアドレスが一致した or ローあるアドレスがANY or 指定ローカルアドレスがANY	ではない
		if ((pcb->local.addr == key1.addr) || (pcb->local.addr == IP_ADDR_ANY) || (key1.addr != IP_ADDR_ANY)) {
			if ((pcb->remote.addr == key2.addr && pcb->remote.port == key2.port) ||		// リモートエンドポイントが一致
				(pcb->remote.addr == IP_ADDR_ANY && pcb->remote.port == 0) ||			// TCP制御ブロックのリモートエンドポイントがany
				(key2.addr == IP_ADDR_ANY && key2.port == 0)) {							// 指定リモートエンドポイントがany
				if (pcb->state != TCP_STATE_LISTEN) {
					return pcb;
				}
				// LISTEN状態の場合は、候補として覚えておく
				candidate = pcb;
			}
		}
	}
	// リモートエンドポイントと完全に一致するものが見つからなかった場合に候補を返す
	return candidate;
}

void TcpPcb(void const * argument)
{
	
}

static struct tcp_pcb * tcp_pcb_alloc(void)
{
	TCP_CB *this = get_myself();
	struct tcp_pcb *pcb;
	
	// 割り当て可能なブロックを検索
	for (pcb = this->pcbs; pcb < tailof(this->pcbs); pcb++) {
		if (pcb->state == TCP_STATE_NONE) {
			pcb->state = TCP_STATE_CLOSED;
			osThreadDef(TcpPcb, TcpPcb, osPriorityLow, 0, 512);
			pcb->thread_id = osThreadCreate(osThread(TcpPcb), NULL);
			return pcb;
		}
	}
	return NULL;
}

// 制御ブロックの開放
static void tcp_pcb_release(struct tcp_pcb * pcb)
{
	TCP_CB *this = get_myself();
	
	// ★タスク削除★
	
	// 制御ブロックのクリア
	memset(pcb, 0, sizeof(*pcb));
	TCP_DBG_PRINT("success, desc=%d", tcp_pcb_desc(pcb));
}

// TCPセグメントの組み立てと出力
static ssize_t tcp_output_segment(uint32_t seq, uint32_t ack, uint8_t flg, uint16_t wnd, const uint8_t *data, size_t len, ip_endp_t local, ip_endp_t remote)
{
	uint8_t buf[IP_PAYLOAD_SIZE_MAX] = {};		// ★ 後で外に出す
	struct tcp_hdr *hdr;
	uint8_t hlen;
	struct pseudo_hdr pseudo;
	uint16_t psum;
	uint16_t total;
	char ep1[IP_ENDP_STR_LEN];
	char ep2[IP_ENDP_STR_LEN];
	
	hdr = (struct tcp_hdr *)buf;
	hdr->src = local.port;
	hdr->dst = remote.port;
	hdr->seq = hton32(seq);
	hdr->ack = hton32(seq);
	hlen = sizeof(*hdr);
	hdr->off = (hlen >> 2) << 4;
	hdr->flg = flg;
	hdr->wnd = hton16(wnd);
	hdr->sum = 0;
	hdr->up = 0;
	memcpy((uint8_t*)hdr + hlen, data, len);
	pseudo.src = local.addr;
	pseudo.dst = remote.addr;
	pseudo.zero = 0;
	pseudo.protocol = IP_PROTOCOL_TCP;
	total = hlen + len;
	pseudo.len = hton16(total);
	psum = ~cksum16((uint16_t*)&pseudo, sizeof(pseudo), 0);
	hdr->sum = cksum16((uint16_t*)hdr, total, psum);
	TCP_DBG_PRINT("%s=>%s, len=%d", ip_endp_ntop(local, ep1, sizeof(ep1)), ip_endp_ntop(remote, ep2, sizeof(ep2)), total);
	tcp_print(buf, total);
	if (ip_output(IP_PROTOCOL_TCP, buf, total, local.addr, remote.addr) == -1) {
		return -1;
	}
	return len;
}

// 到着セグメントの処理
// rfc793 - section 3.9 [Event Processing > SEGMENT ARRIVES]
static void tcp_segment_arrives(struct seg_info *seg, uint8_t flags, const uint8_t *data, size_t len, ip_endp_t local, ip_endp_t remote)
{
	struct tcp_pcb *pcb;
	
	// 制御ブロックの取得
	pcb = tcp_pcb_select(local, remote);
	// 制御ブロックが存在しない or またはCLOSE状態
	// 未使用ポートに対する接続要求か以前に存在したコネクションで使用されたポート宛ての古いセグメントが遅れて届いたと想定
	// // 有効なコネクションが存在しない状態で届いたセグメントはすべて破棄し、RSTフラグを設定した強制切断のセグメントを送信
	if ((pcb == NULL) || (pcb->state == TCP_STATE_CLOSED)) {
		TCP_DBG_PRINT("PCB is %s", pcb ? "closed":"not found");
		// 相手から強制切断の通知が来たら、何もせず処理を中断
		if (TCP_FLG_ISSET(flags, TCP_FLG_RST)) {
			return;
		}
		// ACKアリの場合、送られてきた確認応答番号に合わせてシーケンス番号を設定
		if (TCP_FLG_ISSET(flags, TCP_FLG_ACK)) {
			tcp_output_segment(0, seg->seq + seg->len, TCP_FLG_RST | TCP_FLG_ACK,0 , NULL, 0, local, remote);
		// ACKなしの場合、相手はまだこちらのデータを受信していないため、シーケンス番号は0で送信
		} else {
			tcp_output_segment(seg->ack, 0, TCP_FLG_RST, 0, NULL, 0, local, remote);
		}
		return;
	}
	// later
}

// 入力
static void tcp_input(const struct ip_hdr *iphdr, const uint8_t *data, size_t len, struct ip_iface *iface)
{
	TCP_CB *this = get_myself();
	struct tcp_hdr *hdr;
	struct pseudo_hdr pseudo;
	uint16_t psum;
	ip_endp_t src, dst;
	char ep1[IP_ENDP_STR_LEN];
	char ep2[IP_ENDP_STR_LEN];
	uint8_t hlen;
	struct seg_info seg;
	
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
	hlen = (hdr->off >> 4) << 2;
	seg.seq = ntoh32(hdr->seq);
	seg.ack = ntoh32(hdr->ack);
	seg.len = len - hlen;
	if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_SYN)) {
		seg.len++;
	}
	if (TCP_FLG_ISSET(hdr->flg, TCP_FLG_FIN)) {
		seg.len++;
	}
	seg.wnd = ntoh16(hdr->wnd);
	seg.up = ntoh16(hdr->up);
	osMutexWait(this->lock, osWaitForever);
	tcp_segment_arrives(&seg, hdr->flg, (uint8_t*)hdr+hlen, len - hlen, dst, src);
	osMutexRelease(this->lock);
	
	return;
}

// 初期化
osStatus tcp_init(void)
{
	TCP_CB *this = get_myself();
	
	// 制御ブロックの初期化
	memset(this, 0, sizeof(TCP_CB));
	
	// ミューテックス作成
	osMutexDef(myLock);
	this->lock = osMutexCreate(osMutex(myLock));
	if (this->lock == NULL) {
		return osErrorOS;
	}
	
	// TCPモジュールの登録
	if (ip_protocol_register(IP_PROTOCOL_TCP, tcp_input) != osOK) {
		TCP_ERR_PRINT("ip_protocol_register erorr");
		return osErrorResource;
	}
	return osOK;
}
