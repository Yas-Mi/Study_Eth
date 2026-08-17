/*
 * eth_drv.c
 *
 *  Created on: May 6, 2026
 *      Author: hcuym
 */
#include <string.h>
#include "stm32f7xx.h"
#include "cmsis_os.h"
#include "console.h"
#include "eth.h"
#include "util.h"

#include "eth_drv.h"

#define ETH_RECV_EVENT		(0x00000001)	// 受信イベント

// 制御ブロック
typedef struct {
	ETH_DRV_CH			ch;					// チャネル情報
	uint32_t			status;				// 状態
	osSemaphoreId		tx_sem_id;  	 	// 送信セマフォID
	osThreadId			EthDrvRecvHandle;	// タスクID
} ETH_DRV_CB;
static ETH_DRV_CB eth_drv_cb[ETH_DRV_CH_MAX];
#define get_myself(ch) (&eth_drv_cb[ch])

static const ETH_CH ch_map[ETH_DRV_CH_MAX] = {
	ETH_CH_1,	// ETH_DRV_CH_1
};

const uint8_t ETHER_ADDR_EMPTY[ETHER_ADDR_LEN] = {0, 0, 0, 0, 0, 0};
const uint8_t ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// プロトタイプ
static void rx_cb(void *vp, uint32_t ret);
static void tx_cb(void *vp, uint32_t ret);
static void err_cb(void *vp, uint32_t ret);

// オープンパラメータ
static const ETH_OPEN eth_open_par = {
	COM_MODE_FULL_DUPLEX,
	tx_cb,
	rx_cb,
	err_cb,
	get_myself(ETH_DRV_CH_1),
	{0, 0, 0, 0, 0, 0}
};

// 受信コールバック
static void rx_cb(void *vp, uint32_t ret)
{
	ETH_DRV_CB *this = (ETH_DRV_CB*)vp;
	int32_t signals;
	
	// 受信イベント通知
	signals = osSignalSet (this->EthDrvRecvHandle, ETH_RECV_EVENT);
}

// 送信コールバック
static void tx_cb(void *vp, uint32_t ret)
{
	ETH_DRV_CB *this = (ETH_DRV_CB*)vp;
	
	osSemaphoreRelease(this->tx_sem_id);
}

// エラーコールバック
static void err_cb(void *vp, uint32_t ret)
{
	ETH_DRV_CB *this = (ETH_DRV_CB*)vp;
	
	
}

// MADアドレスのバイナリを文字列に変換
char * ether_drv_addr_ntop(const uint8_t *n, char *p, size_t size)
{
	if ((n == NULL)||(p == NULL)) {
		return NULL;
	}
	snprintf(p, size, "%02x:%02x:%02x:%02x:%02x:%02x", n[0], n[1], n[2], n[3], n[4], n[5]);
	return p;
}

// MACアドレスの文字列をバイナリに変換
int ether_drv_addr_pton(const char *p, uint8_t *n)
{
	int index;
	char *ep;
	long val;
	
	if ((n == NULL)||(p == NULL)) {
		return -1;
	}
	for (index = 0; index < ETHER_ADDR_STR_LEN; index++) {
		val = strtol(p, &ep, 16);
		if ((ep == p) || (val < 0) || (val > 0xFF) || ((index < ETHER_ADDR_LEN - 1) && (*ep != ':'))) {
			break;
		}
		n[index] = (uint8_t)val;
		p = ep + 1;
	}
	if ((index != ETHER_ADDR_LEN) || (*ep != '\n')) {
		return -1;
	}
	return 0;
}

// 受信タスク
static uint8_t rx_buf[1500];
void EthDrvRecv(void const * argument)
{
	ETH_DRV_CB *this =  (ETH_DRV_CB*)argument;
	osEvent evt;
	osStatus ercd;
	uint32_t size;
	
	while (1) {
		// 受信イベントを待つ
		evt = osSignalWait (ETH_RECV_EVENT, 100);
		if (evt.status != osEventSignal) {
			continue;
		}
		// 読みに行く
		if ((ercd = eth_recv(this->ch, rx_buf, &size)) != osOK) {
			continue;
		}
		// 上位層へ通知
	}
}

// 初期化
osStatus eth_drv_init(void)
{
	ETH_DRV_CB *this;
	ETH_DRV_CH ch;
	
	for (ch = 0; ch < ETH_DRV_CH_MAX; ch++) {
		// 制御ブロック取得&初期化
		this =  get_myself(ch);
		memset(this, 0, sizeof(ETH_DRV_CB));
		
		// チャネル情報設定
		this->ch = ch;
		
		// タスク作成
		osThreadDef(EthDrvRecv, EthDrvRecv, osPriorityLow, 0, 512);
		this->EthDrvRecvHandle = osThreadCreate(osThread(EthDrvRecv), this);
		
		// 送信ディスクリプタの数だけセマフォを作成
		osSemaphoreDef(semaphore);
		this->tx_sem_id = osSemaphoreCreate(osSemaphore(semaphore), TX_DISCRIPTOR_NUM);
		
	}

	return osOK;
}

// オープン
osStatus eth_drv_open(ETH_DRV_CH ch, char *mac_addr)
{
	ETH_DRV_CB *this = get_myself(ch);
	osStatus ercd;
	ETH_CH eth_ch;
	ETH_OPEN par;
	
	// パラメータチェック
	if (ch >= ETH_DRV_CH_MAX) {
		return osErrorParameter;
	}
	
	// MACアドレスチェック
	if ((ether_drv_addr_pton(mac_addr, par.mac_addr)) != 0) {
		return osErrorParameter;
	}
	
	// ethのchに変換
	eth_ch = ch_map[ch];
	
	// その他オープンパラメータを設定
	par.mode = COM_MODE_FULL_DUPLEX;
	par.tx_cb = tx_cb;
	par.rx_cb = rx_cb;
	par.err_cb = err_cb;
	par.cb_vp = this;
	
	// オープン
	if ((ercd = eth_open(eth_ch, &par)) != osOK) {
		goto ETH_OPEN_END;
	}
	
ETH_OPEN_END:
	return ercd;
}

// 送信
osStatus eth_drv_send(ETH_DRV_CH ch, uint8_t *p_data, uint16_t size, uint32_t timeout)
{
	ETH_DRV_CB *this = get_myself(ch);
	int32_t ercd;
	osStatus ret;
	ETH_CH eth_ch;
	
	// パラメータチェック
	if (ch >= ETH_DRV_CH_MAX) {
		return osErrorParameter;
	}
	
	// セマフォ取得
	if ((ercd = osSemaphoreWait(this->tx_sem_id, timeout)) < 0) {
		return osErrorOS;
	}
	
	console_printf("sem = %d\n", ercd);
	
	// ethのchに変換
	eth_ch = ch_map[ch];
	
	// 送信
	ret = eth_send(eth_ch, p_data, size);
	
	return ret;
}


