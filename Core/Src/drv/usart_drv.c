/*
 * usart_drv.c
 *
 *  Created on: 2024/10/14
 *      Author: user
 */
#include <string.h>
#include "cmsis_os.h"
#include "usart_drv.h"
#include "usart.h"

// 状態
#define ST_INIT		(0)		// 初期状態
#define ST_CLOSE	(1)		// クローズ状態
#define ST_OPEN		(2)		// オープン状態

// マクロ
#define SLEEP_TIME	(1)	// スリープ時間[ms]

// イベント
#define UART_DRV_SEND_DONE	(0x00000001)
#define UART_DRV_RECV_DONE	(0x00000002)

// 制御ブロック
typedef struct {
	uint32_t		status;
	osThreadId		snd_thread_id;
	osThreadId		rcv_thread_id;
} USART_DRV_CB;
static USART_DRV_CB usart_drv_cb[USART_DRV_DEV_MAX];
#define get_myself(dev)	(&usart_drv_cb[dev])

// USART情報
typedef struct {
	USART_CH		ch;			// チャネル
	USART_OPEN_PAR	open_par;	// オープンパラメータ
} USART_DEV_INFO;

static const USART_DEV_INFO usart_info_tbl[USART_DRV_DEV_MAX] = {
	{USART_CH_1, {USART_LEN_8, USART_STOPBIT_1, USART_PARITY_DISABLE, 115200}},
};

// 受信コールバック
void usart_recv_callback(USART_CH ch, void* p_ctx)
{
	USART_DRV_CB *this = (USART_DRV_CB*)p_ctx;
	
	if (this->rcv_thread_id != NULL) {
		// イベント送信
		osSignalSet(this->rcv_thread_id, UART_DRV_RECV_DONE);
	}
}

// 送信コールバック
void usart_send_callback(USART_CH ch, void* p_ctx)
{
	USART_DRV_CB *this = (USART_DRV_CB*)p_ctx;
	
	if (this->snd_thread_id != NULL) {
		// イベント送信
		osSignalSet(this->snd_thread_id, UART_DRV_SEND_DONE);
	}
}

// エラーコールバック
void usart_err_callback(USART_CH ch, void* p_ctx)
{
	USART_DRV_CB *this = (USART_DRV_CB*)p_ctx;
	
}

// 初期化
osStatus usart_drv_init(void)
{
	USART_DRV_CB *this;
	USART_DRV_DEV dev;
	
	// 初期化
	for (dev = 0; dev < USART_DRV_DEV_MAX; dev++) {
		// 制御ブロックを取得
		this = get_myself(dev);
		// 初期化
		memset(this, 0, sizeof(USART_DRV_CB));
		// 状態をクローズ状態に更新
		this->status = ST_CLOSE;
	}
	
	return usart_init();
}

// オープン
osStatus usart_drv_open(USART_DRV_DEV dev)
{
	USART_DRV_CB *this;
	const USART_DEV_INFO *p_info;
	uint32_t ercd;
	
	// パラメータチェック
	if (dev >= USART_DRV_DEV_MAX) {
		return osErrorParameter;
	}
	
	// 制御ブロック取得
	this = get_myself(dev);
	
	// クローズ状態でなければ終了
	if (this->status != ST_CLOSE) {
		return osErrorParameter;
	}
	
	// USART情報取得
	p_info = &usart_info_tbl[dev];
	
	// オープン
	if ((ercd = usart_open(p_info->ch, &(p_info->open_par), usart_send_callback, usart_recv_callback, usart_err_callback, this)) != osOK) {
		goto EXIT;
	}
	
	// 状態を更新
	this->status = ST_OPEN;
	
EXIT:
	return ercd;
}

// 送信
int32_t usart_drv_send(USART_DRV_DEV dev, uint8_t *p_data, uint32_t size, int32_t tmout)
{
	USART_DRV_CB *this;
	const USART_DEV_INFO *p_info;
	uint32_t ercd;
	uint32_t cnt = 0;
	
	// パラメータチェック
	if (dev >= USART_DRV_DEV_MAX) {
		return -1;
	}
	if (p_data == NULL) {
		return -1;
	}
	
	// 制御ブロック取得
	this = get_myself(dev);
	
	// クローズ状態でなければ終了
	if (this->status != ST_OPEN) {
		return -1;
	}
	
	// タスク情報を取得
	this->snd_thread_id = osThreadGetId();
	
	// USART情報取得
	p_info = &usart_info_tbl[dev];
	
	while(1) {
		// 送信
		if ((ercd = usart_send(p_info->ch, p_data, size)) < 0) {
			goto EXIT;
		} else {
			// 送信できたサイズ分だけ更新
			p_data += ercd;
			size -= ercd;
			cnt += ercd;
			
			// 全データ送信完了
			if (size == 0) {
				ercd = cnt;
				break;
				
			// 送信を待たない
			} else if (tmout == 0) {
				ercd = cnt;
				break;
				
			// 全部送信できていないから待つ場合
			} else if (tmout > 0) {
				// いったんウェイト
				osSignalWait(UART_DRV_SEND_DONE, SLEEP_TIME);
				tmout -= SLEEP_TIME;
				// タイムアウト発生
				if (tmout < 0) {
					ercd = cnt;
					break;
				}
				
			} else {
				// 何もしない
			}
		}
		
	}
	
EXIT:
	this->snd_thread_id = NULL;
	
	return ercd;
}

// 受信
int32_t usart_drv_recv(USART_DRV_DEV dev, uint8_t *p_data, uint32_t size, int32_t tmout)
{
	USART_DRV_CB *this;
	const USART_DEV_INFO *p_info;
	uint32_t ercd;
	uint32_t cnt = 0;
	
	// パラメータチェック
	if (dev >= USART_DRV_DEV_MAX) {
		return -1;
	}
	if (p_data == NULL) {
		return -1;
	}
	
	// 制御ブロック取得
	this = get_myself(dev);
	
	// クローズ状態でなければ終了
	if (this->status != ST_OPEN) {
		return -1;
	}
	
	// タスク情報を取得
	this->rcv_thread_id = osThreadGetId();
	
	// USART情報取得
	p_info = &usart_info_tbl[dev];
	
	while(1) {
		// 送信
		if ((ercd = usart_recv(p_info->ch, p_data, size)) < 0) {
			goto EXIT;
		} else {
			// 受信できたサイズ分だけ更新
			p_data += ercd;
			size -= ercd;
			cnt += ercd;
			
			// 全データ受信完了
			if (size == 0) {
				ercd = cnt;
				break;
				
			// 受信を待たない
			} else if (tmout == 0) {
				ercd = cnt;
				break;
				
			// 全部送信できていないから待つ場合
			} else if (tmout > 0) {
				// いったんウェイト
				osSignalWait(UART_DRV_RECV_DONE, SLEEP_TIME);
				tmout -= SLEEP_TIME;
				// タイムアウト発生
				if (tmout < 0) {
					ercd = cnt;
					break;
				}
				
			} else {
				// 何もしない
			}
		}
		
	}
	
EXIT:
	this->rcv_thread_id = NULL;
	
	return ercd;
	
}
