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

#include "eth.h"

// イベント
#define OPEN_EVENT		(0x01)
#define SEND_ARP_EVENT	(0x02)
#define GO_STOP_EVENT	(0x04)
#define ALL_EVENT		(OPEN_EVENT|SEND_ARP_EVENT|GO_STOP_EVENT)

// イベント関数
typedef void (*EVENT_FUNC)(void);

__attribute__((aligned(32)))
uint8_t arp_request_frame[] = {
    // === Ethernet Header ===
    0xff,0xff,0xff,0xff,0xff,0xff,   // Destination MAC (Broadcast)
    0x00,0x11,0x22,0x33,0x44,0x55,   // Source MAC (STM32)
    0x08,0x06,                       // EtherType = ARP

    // === ARP Header ===
    0x00,0x01,                       // Hardware type = Ethernet
    0x08,0x00,                       // Protocol type = IPv4
    0x06,                            // Hardware size = 6
    0x04,                            // Protocol size = 4
    0x00,0x01,                       // Opcode = 1 (request)

    // === ARP Payload ===
    // Sender MAC (STM32)
    0x00,0x11,0x22,0x33,0x44,0x55,

    // Sender IP (STM32 = 169.254.87.98)
    0xa9,0xfe,0x57,0x62,

    // Target MAC (unknown)
    0x00,0x00,0x00,0x00,0x00,0x00,

    // Target IP (PC = 169.254.87.97)
    0xa9,0xfe,0x57,0x61,

    // Padding (Ethernet minimum 60 bytes)
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00
};

static const ETH_OPEN eth_open_par = {
	COM_MODE_FULL_DUPLEX,
};

// 制御ブロック
typedef struct {
	osThreadId 		RecvTaskHandle;		// コンソール受信タスク
	osMailQId		MailHandle;			// コンソール送信バッファ
	osThreadId		EthTestSendHandle;	// タスクID
	osThreadId		EthTestRecvHandle;	// タスクID
} ETH_TEST_CB;
static ETH_TEST_CB eth_test_cb;
#define get_myself() (&eth_test_cb)

static uint8_t rx_buf[1518];

// 受信タスク
void EthTestRecv(void const * argument)
{
	ETH_TEST_CB *this =  get_myself();
	osStatus ercd;
	uint32_t size;
	uint32_t i;
	
	while (1) {
		if ((ercd = eth_recv(rx_buf, &size)) != osOK) {
			continue;
		}
		console_printf("size:%d\n", size);
		for (i = 0; i < size; i++) {
			if (size > 64) {
				break;
			}
			console_printf("0x%x ", rx_buf[i]);
		}
		console_printf("\n");
	}
}

// open
static void open(void)
{
	osStatus ercd;
	
	// 送信
	ercd = eth_open(&eth_open_par);
	console_printf("eth_open:ercd = %d\n", ercd);
}

// arp送信
static void send_arp(void)
{
	osStatus ercd;
	uint8_t arp_frame[64] __attribute__((aligned(32))) = {0};
	
	memcpy(arp_frame, arp_request_frame, sizeof(arp_request_frame));
	
	// 送信
	ercd = eth_send(arp_frame, sizeof(arp_request_frame));
	console_printf("eth_send:ercd = %d\n", ercd);
}

// open
static void go_stop(void)
{
	osStatus ercd;
	
	// ストップモード以降
	ercd = eth_go_down(&eth_open_par);
	console_printf("eth_open:ercd = %d\n", ercd);
}

// イベント処理
static const EVENT_FUNC event_func[] = {
	open,		// OPEN_EVENT
	send_arp,	// SEND_ARP_EVENT
	go_stop,	// GO_STOP_EVENT
};

// 送信タスク
void EthTestSend(void const * argument)
{
	osEvent evt;
	uint8_t evt_idx;
	EVENT_FUNC evt_func;
	
	while (1) {
		// イベント待機
		evt = osSignalWait(ALL_EVENT, osWaitForever);
		// イベント受信
		if (evt.status == osEventSignal) {
			// クリア
			//osSignalClear(this->CanMngHandle, evt.value.signals);
			// イベント関数テーブルのインデックスに変換
			for (evt_idx = 0; evt_idx < 32; evt_idx++) {
				if (((evt.value.signals) & (1UL << evt_idx)) != 0) {
					// イベント処理実行
					evt_func = event_func[evt_idx];
					evt_func();
				}
			}
		}
	}
}

// 初期化
osStatus eth_test_init(void)
{
	ETH_TEST_CB *this =  get_myself();
	uint32_t ercd;
	
	// メールキュー作成
	osMailQDef(EthTestRcvBuf, 32, 128);
	osMailCreate(osMailQ(EthTestRcvBuf), NULL);
	
	osMailQDef(EthTestSndBuf, 32, 128);
	osMailCreate(osMailQ(EthTestSndBuf), NULL);
	
	// タスク作成
	osThreadDef(EthTestRecv, EthTestRecv, osPriorityLow, 0, 512);
	this->EthTestRecvHandle = osThreadCreate(osThread(EthTestRecv), NULL);
	
	osThreadDef(EthTestSend, EthTestSend, osPriorityLow, 0, 512);
	this->EthTestSendHandle = osThreadCreate(osThread(EthTestSend), NULL);
	
EXIT:
	return ercd;
}

// コマンド
static void eth_test_cmd_open(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	
	// イベントセット
	return osSignalSet(this->EthTestSendHandle, OPEN_EVENT);
}

// コマンド
static void eth_test_cmd_arp(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	
	// イベントセット
	return osSignalSet(this->EthTestSendHandle, SEND_ARP_EVENT);
}

static void eth_test_cmd_go_stop(int argc, char *argv[])
{
	ETH_TEST_CB *this =  get_myself();
	
	// イベントセット
	return osSignalSet(this->EthTestSendHandle, GO_STOP_EVENT);
}

// コマンド設定関数
void eth_test_set_cmd(void)
{
	COMMAND_INFO cmd;
	
	// コマンドの設定
	cmd.input = "eth_open";
	cmd.func = eth_test_cmd_open;
	console_set_command(&cmd);
	cmd.input = "eth_arp";
	cmd.func = eth_test_cmd_arp;
	console_set_command(&cmd);
	cmd.input = "eth_go_stop";
	cmd.func = eth_test_cmd_go_stop;
	console_set_command(&cmd);
}

