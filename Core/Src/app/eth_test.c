/*
 * eth_test.c
 *
 *	Created on: Jan 1, 2026
 *		Author: user
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
#include "loopback.h"
#include "netif.h"
#include "icmp.h"
#include "udp.h"
#include "tcp.h"

#include "eth_drv.h"

// イベント
#define UDP_OPEN_EVENT		(0)
#define UDP_BIND_EVENT		(1)
#define UDP_CLOSE_EVENT		(2)
#define UDP_RCV_EVENT		(3)
#define UDP_SND_EVENT		(4)
#define TCP_OPEN_EVENT		(5)
#define TCP_CLOSE_EVENT		(6)

// イベント関数
typedef void (*EVENT_FUNC)(void *data);

#define LOOPBACK_IP_ADDR	"127.0.0.1"
#define LOOPBACK_NETMASK	"255.0.0.0"
#define DEFAULT_GATEWAY 	"192.0.2.1"

#define DESC_NUM			(8)

#define QUEUE_SIZE			(128)
#define QUEUE_LENGTH		(32)

// BINDパラメータ
typedef struct {
	int32_t	desc;
	char	endp[IP_ENDP_STR_LEN];
} BIND;

// OPENパラメータ
typedef struct {
	int32_t	active;
	char	local[IP_ENDP_STR_LEN];
	char	remote[IP_ENDP_STR_LEN];
} OPEN;

// メールキュー
typedef struct {
	uint8_t	event;
	uint32_t data[(QUEUE_SIZE/4)-1];	// MAILQUEUE全体のサイズをQUEUE_SIZEにしたい
} MAILQUEUE;

// 制御ブロック
typedef struct {
	osMailQId		MailHandle;
	osThreadId		EthTestHandle;	// タスクID
	int32_t 		desc[DESC_NUM];
	uint8_t			desc_num;
} ETH_TEST_CB;
static ETH_TEST_CB eth_test_cb;
#define get_myself() (&eth_test_cb)

struct net_device *g_dev;

static const char send_test_buf[] = "mito is very strong";

// 受信用タスク
void Receiver(void const * argument)
{
	int32_t desc;
	uint8_t buf[128];
	ip_endp_t remote;
	ssize_t n;
	char endp[IP_ENDP_STR_LEN];
	
	desc = *((int32_t*)argument);
	infof("runnning(%d),,,", desc);
	
	while (1) {
		n = udp_cmd_recvfrom(desc, buf, sizeof(buf), &remote);
		if (n == -1) {
			warnf("udp_cmd_recvfrom() failure");
			break;
		}
		infof("%d bytes data receive form %s", n, ip_endp_ntop(remote, endp, sizeof(endp)));
	}
}

// microps
static osStatus setup(void)
{
	struct ip_iface *iface;
	struct net_device *dev;
	
	infof("setup prorcol stack...");
	if (net_init() !=  osOK) {
		errorf("net_init() failure");
		return osErrorOS;
	}
	// ループバック
	g_dev = loopback_init();
	if (g_dev == NULL) {
		errorf("loopback_init() failure");
		return osErrorOS;
	}
	iface = ip_iface_alloc(LOOPBACK_IP_ADDR, LOOPBACK_NETMASK);
	if (iface == NULL) {
		errorf("ip_iface_alloc() failure");
		return osErrorOS;
	}
	if (ip_iface_register(g_dev, iface) != osOK) {
		errorf("ip_iface_register() failure");
		return osErrorOS;
	}
	// eth
	dev = netif_init("eth", "00:00:5e:00:53:01\n");
	if (dev == NULL) {
		errorf("loopback_init() failure");
		return osErrorOS;
	}
	iface = ip_iface_alloc("192.0.2.2", "255.255.255.0");
	if (iface == NULL) {
		errorf("ip_iface_alloc() failure");
		return osErrorOS;
	}
	if (ip_iface_register(dev, iface) != osOK) {
		errorf("ip_iface_register() failure");
		return osErrorOS;
	}
	if (ip_route_set_default_gateway(iface, DEFAULT_GATEWAY) != osOK) {
		errorf("ip_route_set_default_gateway() failure");
		return osErrorOS;
	}
	if (net_run() !=  osOK) {
		errorf("net_init() failure");
		return osErrorOS;
	}
	return osOK;
}

static osStatus cleanup(void)
{
	infof("cleanup prorcol stack...");
	if (net_shutdown() !=  osOK) {
		errorf("net_shutdown() failure");
		return osErrorOS;
	}
	return osOK;
}

// open
static void udp_open(void *data)
{
	ETH_TEST_CB *this =  get_myself();
	int32_t desc;
	
	// デスク数チェック
	if (this->desc_num >= DESC_NUM) {
		errorf("desc num over");
		return;
	}
	
	// オープン
	desc = udp_cmd_open();
	if (desc < 0) {
		errorf("udp_cmd_open failure");
		return;
	}
	
	console_printf("open succsess! desc=%d\n", desc);
}

// bind
static void udp_bind(void *data)
{
	ETH_TEST_CB *this =  get_myself();
	osStatus ercd;
	BIND *par;
	ip_endp_t local;
	char endp[IP_ENDP_STR_LEN];
	osThreadId id;
	
	// キャスト
	par = (BIND*)data;
	
	// bind
	ip_endp_pton(par->endp, &local);
	if (udp_cmd_bind(par->desc, local) != osOK) {
		errorf("udp_cmd_bind failure");
		udp_cmd_close(par->desc);
		return;
	}
	
	// 表示
	memcpy(endp, par->endp, sizeof(endp));
	endp[IP_ENDP_STR_LEN-1] = '\0';
	console_printf("bind succsess! desc=%d, endp=%s\n", par->desc, endp);
	
	// デスクを覚えておく
	this->desc[this->desc_num] = par->desc;
	
	// 受信用タスク作成
	osThreadDef(Receiver, Receiver, osPriorityLow, 0, 512);
	id = osThreadCreate(osThread(Receiver), &(this->desc[this->desc_num]));
	if (id == NULL) {
		errorf("receiver task not created");
		udp_cmd_close(par->desc);
		return;
	}
	
	this->desc_num++;
}

// close
static void udp_close(void *data)
{
	int32_t desc = *((int32_t*)data);
	
	// クローズ
	desc = udp_cmd_close(desc);
	if (desc < 0) {
		errorf("udp_cmd_close failure");
		return;
	}
	
	console_printf("close succsess! desc=%d\n", desc);
}

// close
static void udp_recv(void *data)
{
	// nop
}

// close
static void udp_send(void *data)
{
	ssize_t n;
	BIND *par;
	ip_endp_t remote;
	
	// キャスト
	par = (BIND*)data;
	
	// sendto
	ip_endp_pton(par->endp, &remote);
	n = udp_cmd_sendto(par->desc, send_test_buf, sizeof(send_test_buf), remote);
	if (n < 0) {
		errorf("udp_cmd_sendto failure");
	}
}

// open
static void tcp_open(void *data)
{
	ETH_TEST_CB *this =  get_myself();
	int32_t desc;
	ip_endp_t local, remote;
	OPEN *par;
	
	// デスク数チェック
	if (this->desc_num >= DESC_NUM) {
		errorf("desc num over");
		return;
	}
	
	// キャスト
	par = (OPEN*)data;
	
	// 変換
	ip_endp_pton(par->local, &local);
	ip_endp_pton(par->remote, &remote);
	
	// オープン
	desc = tcp_cmd_open(local, remote, par->active);
	if (desc < 0) {
		errorf("tcp_cmd_open failure");
		return;
	}
	
	console_printf("open succsess! desc=%d\n", desc);
}

// close
static void tcp_close(void *data)
{
	int32_t desc = *((int32_t*)data);
	
	// クローズ
	desc = tcp_cmd_close(desc);
	if (desc < 0) {
		errorf("udp_cmd_close failure");
		return;
	}
	
	console_printf("close succsess! desc=%d\n", desc);
}

// イベント処理
static const EVENT_FUNC event_func[] = {
	udp_open,		// UDP_OPEN_EVENT
	udp_bind,		// UDP_BIND_EVENT
	udp_close,		// UDP_CLOSE_EVENT
	udp_recv,		// UDP_RCV_EVENT
	udp_send,		// UDP_SND_EVENT
	tcp_open,		// TCP_OPEN_EVENT
	tcp_close,		// TCP_CLOSE_EVENT
};

// テスト用タスク
void EthTestTask(void const * argument)
{
	ETH_TEST_CB *this =  get_myself();
	osEvent evt;
	MAILQUEUE *rcv;
	EVENT_FUNC f;
	
	while (1) {
		// メール待機
		evt = osMailGet(this->MailHandle, osWaitForever);
		// メール受信
		if (evt.status == osEventMail) {
			// イベント処理実行
			rcv = (MAILQUEUE*)evt.value.p;;
			f = event_func[rcv->event];
			f(rcv->data);
			// 解放
			osMailFree(this->MailHandle, rcv);
		}
	}
}

// 初期化
osStatus eth_test_init(void)
{
	ETH_TEST_CB *this =  get_myself();
	uint32_t ercd = osOK;
	
	// 制御ブロック初期化
	memset(this, 0, sizeof(ETH_TEST_CB));
	
	// メールキュー作成
	// 128byteのデータが32個
	osMailQDef(EthTestTaskBuf, QUEUE_LENGTH, QUEUE_SIZE);
	this->MailHandle = osMailCreate(osMailQ(EthTestTaskBuf), NULL);
	
	// テスト用タスク作成
	osThreadDef(EthTestTask, EthTestTask, osPriorityLow, 0, 512);
	this->EthTestHandle = osThreadCreate(osThread(EthTestTask), NULL);
	
	// プロトコルスタック初期化
	setup();
	
	return ercd;
}

// コマンド
static void eth_test_cmd_udp_open(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	MAILQUEUE *queue;
	
	// バッファ取得
	queue = (MAILQUEUE*)osMailAlloc(this->MailHandle, 0);
	if (queue == NULL) {
		errorf("osMailAlloc() failure");
		return;
	}
	
	// 設定
	queue->event = UDP_OPEN_EVENT;
	
	// メッセージ送信
	osMailPut(this->MailHandle, queue);
	
	return;
}

// コマンド
static void eth_test_cmd_udp_bind(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	MAILQUEUE *queue;
	BIND *bind;
	
	// 引数チェック
	if (argc < 3) {
		console_printf("%s <desc> <ip:port>\n", argv[0]);
		return;
	}
	
	// バッファ取得
	queue = (MAILQUEUE*)osMailAlloc(this->MailHandle, 0);
	if (queue == NULL) {
		errorf("osMailAlloc() failure");
		return;
	}
	
	// 設定
	queue->event = UDP_BIND_EVENT;
	bind = (BIND*)queue->data;
	bind->desc = atoi(argv[1]);
	memcpy(bind->endp, argv[2], sizeof(bind->endp));
	
	// メッセージ送信
	osMailPut(this->MailHandle, queue);
	
	return;
}

static void eth_test_cmd_udp_close(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	MAILQUEUE *queue;
	
	// 引数チェック
	if (argc < 2) {
		console_printf("%s <desc> <ip:port>\n");
		return;
	}
	
	// バッファ取得
	queue = (MAILQUEUE*)osMailAlloc(this->MailHandle, 0);
	if (queue == NULL) {
		errorf("osMailAlloc() failure");
		return;
	}
	
	// 設定
	queue->event = UDP_CLOSE_EVENT;
	queue->data[0] = atoi(argv[1]);
	
	// メッセージ送信
	osMailPut(this->MailHandle, queue);
	
	return;
}

// コマンド
static void eth_test_cmd_udp_send(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	MAILQUEUE *queue;
	BIND *bind;
	
	// 引数チェック
	if (argc < 3) {
		console_printf("%s <desc> <ip:port>\n", argv[0]);
		return;
	}
	
	// バッファ取得
	queue = (MAILQUEUE*)osMailAlloc(this->MailHandle, 0);
	if (queue == NULL) {
		errorf("osMailAlloc() failure");
		return;
	}
	
	// 設定
	queue->event = UDP_SND_EVENT;
	bind = (BIND*)queue->data;
	bind->desc = atoi(argv[1]);
	memcpy(bind->endp, argv[2], sizeof(bind->endp));
	
	// メッセージ送信
	osMailPut(this->MailHandle, queue);
	
	return;
}

// コマンド
static void eth_test_cmd_tcp_open(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	MAILQUEUE *queue;
	OPEN *open;
	
	// 引数チェック
	if (argc < 4) {
		console_printf("%s <active> <localip:port> <remoteip:port>\n", argv[0]);
		return;
	}
	
	// バッファ取得
	queue = (MAILQUEUE*)osMailAlloc(this->MailHandle, 0);
	if (queue == NULL) {
		errorf("osMailAlloc() failure");
		return;
	}
	
	// 設定
	queue->event = TCP_OPEN_EVENT;
	open = (OPEN*)queue->data;
	open->active = atoi(argv[1]);
	memcpy(open->local, argv[2], sizeof(open->local));
	memcpy(open->remote, argv[3], sizeof(open->remote));
	
	// メッセージ送信
	osMailPut(this->MailHandle, queue);
	
	return;
}

// クローズ
static void eth_test_cmd_tcp_close(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	MAILQUEUE *queue;
	
	// 引数チェック
	if (argc < 2) {
		console_printf("%s <desc> <ip:port>\n");
		return;
	}
	
	// バッファ取得
	queue = (MAILQUEUE*)osMailAlloc(this->MailHandle, 0);
	if (queue == NULL) {
		errorf("osMailAlloc() failure");
		return;
	}
	
	// 設定
	queue->event = TCP_CLOSE_EVENT;
	queue->data[0] = atoi(argv[1]);
	
	// メッセージ送信
	osMailPut(this->MailHandle, queue);
	
	return;
}

// コマンド設定関数
void eth_test_set_cmd(void)
{
	COMMAND_INFO cmd;
	
	// コマンドの設定
	cmd.input = "eth_udp_open";
	cmd.func = eth_test_cmd_udp_open;
	console_set_command(&cmd);
	cmd.input = "eth_udp_bind";
	cmd.func = eth_test_cmd_udp_bind;
	console_set_command(&cmd);
	cmd.input = "eth_udp_close";
	cmd.func = eth_test_cmd_udp_close;
	console_set_command(&cmd);
	cmd.input = "eth_udp_send";
	cmd.func = eth_test_cmd_udp_send;
	console_set_command(&cmd);
	cmd.input = "eth_tcp_open";
	cmd.func = eth_test_cmd_tcp_open;
	console_set_command(&cmd);
	cmd.input = "eth_tcp_close";
	cmd.func = eth_test_cmd_tcp_close;
	console_set_command(&cmd);
}

