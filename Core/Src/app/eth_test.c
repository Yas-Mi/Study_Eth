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

#include "eth_drv.h"

// イベント
#define OPEN_EVENT		(0)
#define BIND_EVENT		(1)
#define CLOSE_EVENT		(2)

// イベント関数
typedef void (*EVENT_FUNC)(void *data);

#define LOOPBACK_IP_ADDR	"127.0.0.1"
#define LOOPBACK_NETMASK	"255.0.0.0"
#define DEFAULT_GATEWAY 	"192.0.2.1"

#define DESC_NUM			(5)

#define QUEUE_SIZE			(128)
#define QUEUE_LENGTH		(32)

// BINDパラメータ
typedef struct {
	int32_t	desc;
	char	endp[IP_ENDP_STR_LEN];
} BIND;

// メールキュー
typedef struct {
	uint8_t	event;
	uint32_t data[(QUEUE_SIZE/4)-1];	// MAILQUEUE全体のサイズをQUEUE_SIZEにしたい
} MAILQUEUE;

// 制御ブロック
typedef struct {
	osMailQId		MailHandle;
	osThreadId		EthTestHandle;	// タスクID
} ETH_TEST_CB;
static ETH_TEST_CB eth_test_cb;
#define get_myself() (&eth_test_cb)

static uint8_t rx_buf[1518];
struct net_device *g_dev;

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
static void open(void *data)
{
	int32_t desc;
	
	// オープン
	desc = udp_cmd_open();
	if (desc < 0) {
		errorf("udp_cmd_open failure");
		return;
	}
	
	console_printf("open succsess! desc=%d\n", desc);
}

// bind
static void bind(void *data)
{
	osStatus ercd;
	BIND *par;
	ip_endp_t local;
	char endp[IP_ENDP_STR_LEN];
	
	// キャスト
	par = (BIND*)data;
	
	// bind
	ip_endp_pton(par->endp, &local);
	if (udp_cmd_bind(par->desc, local) != osOK) {
		errorf("udp_cmd_bind failure");
		udp_cmd_close(par->desc);
		return;
	}
	
	memcpy(endp, par->endp, sizeof(endp));
	endp[IP_ENDP_STR_LEN-1] = '\0';
	console_printf("bind succsess! desc=%d, endp=%s\n", par->desc, endp);
}

// open
static void close(void *data)
{
	osStatus ercd;
	int32_t desc = (int32_t*)data;
	
	// オープン
	desc = udp_cmd_close(desc);
	if (desc < 0) {
		errorf("udp_cmd_close failure");
		return;
	}
	
	console_printf("close succsess! desc=%d\n", desc);
}

// イベント処理
static const EVENT_FUNC event_func[] = {
	open,		// OPEN_EVENT
	bind,		// BIND_EVENT
	close,		// CLOSE_EVENT
};

// 送信タスク
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
	uint32_t ercd;
	
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
	queue->event = OPEN_EVENT;
	
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
	queue->event = BIND_EVENT;
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
	queue->event = CLOSE_EVENT;
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
}

