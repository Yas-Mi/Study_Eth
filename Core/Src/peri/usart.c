/*
 * usart.c
 *
 *  Created on: 2024/10/09
 *      Author: user
 */
#include <string.h>
#include "stm32f7xx.h"
#include "stm32f7xx_hal_rcc.h"
#include "cmsis_os.h"
#include "iodefine.h"
#include "usart.h"


// マクロ
#define BUFF_SIZE	(2048)	// リングバッファのサイズ
#define USART_CLK	(96*1000*1000)	// 96MHz

// 状態定義
#define ST_INIT		(0)		// 初期状態
#define ST_CLOSE	(1)		// クローズ状態
#define ST_OPEN		(2)		// オープン状態
#define ST_MAX		(3)		// 最大値

// リングバッファ定義
typedef struct {
	uint8_t		data[BUFF_SIZE];	// データ
	uint32_t	w_idx;				// ライトインデックス
	uint32_t	r_idx;				// リードインデックス
} RING_BUFF;

// 制御ブロック
typedef struct {
	uint32_t		status;		// 状態
	RING_BUFF		snd_buf;	// 送信バッファ
	RING_BUFF		rcv_buf;	// 受信バッファ
	USART_CALLBACK	recv_cb;
	USART_CALLBACK	send_cb;
	USART_CALLBACK	err_cb;
	void*			p_ctx;
} USART_CB;
static USART_CB usart_cb[USART_CH_MAX];
#define get_myself(ch) (&usart_cb[ch])

// チャネル情報
typedef struct {
	USART_TypeDef	*base_addr;	// ベースアドレス
	IRQn_Type		irqn;		// 割り込み番号
	uint32_t		priority;	// 割り込み優先度
	uint32_t		clk;		// クロック
} CH_INFO;
static const CH_INFO ch_info_tbl[USART_CH_MAX] = {
	{USART1,	USART1_IRQn,	5},
	{USART2,	USART2_IRQn,	5},
//	{USART3,	USART3_IRQn,	0},
//	{UART4,		UART4_IRQn,		5},
//	{UART5,		UART5_IRQn,		0},
//	{USART6,	USART6_IRQn,	0},
//	{UART7,		UART7_IRQn,		0},
//	{UART8,		UART8_IRQn,		0},
};
#define get_reg(ch)		(ch_info_tbl[ch].base_addr)
#define get_irqn(ch)	(ch_info_tbl[ch].irqn)
#define get_pri(ch)		(ch_info_tbl[ch].priority)
#define get_clk(ch)		(ch_info_tbl[ch].clk)

// レジスタ設定値
// length
static const int32_t length_reg_config_tbl[USART_LEN_MAX] = {
	0UL,			// USART_LEN_8
	USART_CR1_M,	// USART_LEN_7
};
// parity
static const int32_t parity_reg_config_tbl[USART_PARITY_MAX] = {
	0UL,							// USART_PARITY_DISABLE
	USART_CR1_PCE,					// USART_PARITY_EVEN
	(USART_CR1_PCE | USART_CR1_PS),	// USART_PARITY_ODD
};
// stopbit
static const int32_t stopbit_reg_config_tbl[USART_STOPBIT_MAX] = {
	0UL,							// USART_STOPBIT_1
	USART_CR2_STOP_1,				// USART_STOPBIT_2
};

// 共通割り込み処理
void usart_common_handler(USART_CH ch)
{
	USART_CB *this = get_myself(ch);
	USART_TypeDef *p_reg;
	RING_BUFF *p_ring_buf;
	uint8_t recv_data;
	
	// ベースレジスタ取得
	p_reg = get_reg(ch);
	
	// エラーチェック
	if (p_reg->ISR & (USART_ISR_PE | USART_ISR_FE | USART_ISR_NE | USART_ISR_ORE)) {
		// エラーフラグのクリア
		set_bit(p_reg->ICR, USART_ICR_PECF);
		set_bit(p_reg->ICR, USART_ICR_FECF);
		set_bit(p_reg->ICR, USART_ICR_NCF);
		set_bit(p_reg->ICR, USART_ICR_ORECF);
		// エラーコールバック通知
		if (this->err_cb != NULL) {
			this->err_cb(ch, this->p_ctx);
		}
		return;
	}
	
	// 受信データあり
	if (p_reg->ISR & USART_ISR_RXNE) {
		// 受信データ取得
		recv_data = p_reg->RDR;
		//リングバッファ情報を取得
		p_ring_buf = &(this->rcv_buf);
		// リングバッファに書き込み
		p_ring_buf->data[p_ring_buf->w_idx] = recv_data;
		p_ring_buf->w_idx = (p_ring_buf->w_idx + 1) & (BUFF_SIZE - 1);
		// 上書き発生
		if (p_ring_buf->w_idx == p_ring_buf->r_idx) {
			p_ring_buf->r_idx = (p_ring_buf->r_idx + 1) & (BUFF_SIZE - 1);
		}
		// コールバック通知
		if (this->recv_cb != NULL) {
			this->recv_cb(ch, this->p_ctx);
		}
	}
	
	// 送信データがある
	p_ring_buf = &(this->snd_buf);
	if (p_ring_buf->w_idx != p_ring_buf->r_idx) {
		// 送信レジスタが空いている
		if ((p_reg->ISR & USART_ISR_TXE) != 0) {
			// 送信レジスタにデータをセット
			p_reg->TDR = p_ring_buf->data[p_ring_buf->r_idx];
			p_ring_buf->r_idx = (p_ring_buf->r_idx + 1) & (BUFF_SIZE - 1);
			// コールバック通知
			if (this->send_cb != NULL) {
				this->send_cb(ch, this->p_ctx);
			}
		}
	// 送信データがない
	} else {
		clr_bit(p_reg->CR1, USART_CR1_TXEIE_Pos);
	}
}
// 割り込みハンドラ
void USART1_IRQHandler(void)
{
	usart_common_handler(USART_CH_1);
}
void USART2_IRQHandler(void)
{
	usart_common_handler(USART_CH_2);
}
//void USART3_IRQHandler(void)
//{
//	usart_common_handler(USART_CH_3);
//}
//void UART4_IRQHandler(void)
//{
//	usart_common_handler(USART_CH_4);
//}
//void UART5_IRQHandler(void)
//{
//	usart_common_handler(USART_CH_5);
//}
//void USART6_IRQHandler(void)
//{
//	usart_common_handler(USART_CH_6);
//}
//void UART7_IRQHandler(void)
//{
//	usart_common_handler(USART_CH_7);
//}
//void UART8_IRQHandler(void)
//{
//	usart_common_handler(USART_CH_8);
//}
// コンフィグ
int32_t usart_config(USART_CH ch, USART_OPEN_PAR *p_open_par)
{
	USART_TypeDef *p_reg;
	uint32_t peri_clk;
	
	// パラメータチェック
	if ((p_open_par->len >= USART_LEN_MAX) ||			// 長さチェック
		(p_open_par->stopbit >= USART_STOPBIT_MAX) ||	// ストップビットチェック
		(p_open_par->parity >= USART_PARITY_MAX)) {	// パリティチェック
		return osErrorParameter;
	}
	
	// ベースレジスタ取得
	p_reg = get_reg(ch);
	
	// クロック設定
	//peri_clk = HAL_RCCEx_GetPeriphCLKFreq(get_clk(ch));
	//p_reg->BRR = peri_clk/p_open_par->baudrate;
	p_reg->BRR = USART_CLK/p_open_par->baudrate;
	
	// データ長、パリティ設定
	p_reg->CR1 |= length_reg_config_tbl[p_open_par->len];
	p_reg->CR1 |= parity_reg_config_tbl[p_open_par->parity];
	
	// ストップビット設定
	p_reg->CR2 |=  stopbit_reg_config_tbl[p_open_par->stopbit];
	
	// USART有効
	set_bit(p_reg->CR1, USART_CR1_UE_Pos);
	
	// 割り込み有効
	set_bit(p_reg->CR1, USART_CR1_PEIE_Pos);
	set_bit(p_reg->CR1, USART_CR1_RXNEIE_Pos);
	set_bit(p_reg->CR1, USART_CR1_RE_Pos);
	set_bit(p_reg->CR1, USART_CR1_TE_Pos);
	
	// 割り込み有効
    HAL_NVIC_SetPriority(get_irqn(ch), get_pri(ch), 0);
    HAL_NVIC_EnableIRQ(get_irqn(ch));
	
	return osOK;
}

// 初期化
osStatus usart_init(void)
{
	USART_CB *this;
	USART_CH ch;
	
	for (ch = 0; ch < USART_CH_MAX; ch++) {
		// 制御ブロックの取得
		this = get_myself(ch);
		// 制御ブロックのクリア
		memset(this, 0, sizeof(USART_CB));
		// ★割り込みの登録★
		
		// クローズ状態に更新
		this->status = ST_CLOSE;
	}
	
	return osOK;
}

// オープン関数
osStatus usart_open(USART_CH ch, USART_OPEN_PAR *p_open_par, USART_CALLBACK send_cb, USART_CALLBACK recv_cb, USART_CALLBACK err_cb, void* p_ctx)
{
	USART_CB *this;
	osStatus ercd;
	
	// パラメータチェック
	if (ch >= USART_CH_MAX) {
		return osErrorParameter;
	}
	if (p_open_par == NULL) {
		return osErrorParameter;
	}
	
	// 制御ブロック取得
	this = get_myself(ch);
	
	// クローズ状態でなければ終了
	if (this->status != ST_CLOSE) {
		return osErrorParameter;
	}
	
	// レジスタ設定
	if ((ercd = usart_config(ch, p_open_par)) != osOK) {
		goto EXIT;
	}
	
	// コールバック設定
	this->send_cb = send_cb;
	this->recv_cb = recv_cb;
	this->err_cb = err_cb;
	this->p_ctx = p_ctx;
	
	// 状態をオープンにする
	this->status = ST_OPEN;
	
EXIT:
	return ercd;
}

// クローズ関数
osStatus usart_close(USART_CH ch)
{
	// ★後で実装★
	return osOK;
}

// 送信関数
int32_t usart_send(USART_CH ch, uint8_t *p_data, uint32_t size)
{
	USART_CB *this;
	RING_BUFF *p_ring_buf;
	USART_TypeDef *p_reg;
	uint32_t send_sz = size;
	
	// パラメータチェック
	if (ch >= USART_CH_MAX) {
		return -1;
	}
	if (p_data == NULL) {
		return -1;
	}
	
	// 制御ブロック取得
	this = get_myself(ch);
	
	// オープン状態でなければ終了
	if (this->status != ST_OPEN) {
		return -1;
	}
	
	// ベースレジスタ取得
	p_reg = get_reg(ch);
	
	// 割り込み禁止
	__disable_irq();
	
	// リングバッファ情報取得
	p_ring_buf = &(this->snd_buf);
	
	do {
		// 空いている場合は詰める
		if ((p_ring_buf->w_idx + 1) != p_ring_buf->r_idx) {
			p_ring_buf->data[p_ring_buf->w_idx] = *(p_data++);
			p_ring_buf->w_idx = (p_ring_buf->w_idx + 1) & (BUFF_SIZE - 1);
			size--;
		// 空いていない場合は抜ける
		} else {
			break;
		}
	} while (size > 0);
	
	// 送信サイズ更新
	// (*)サイズはデクリメントしているため、送信したいサイズから引けば送信サイズが出る
	send_sz -= size;
	// 送信割り込み有効
	if (send_sz > 0) {
		set_bit(p_reg->CR1, USART_CR1_TXEIE_Pos);
	}
	
	// 割り込み禁止解除
	__enable_irq();
	
	return send_sz;
}

// 受信関数
int32_t usart_recv(USART_CH ch, uint8_t *p_data, uint32_t size)
{
	USART_CB *this;
	RING_BUFF *p_ring_buf;
	uint32_t recv_sz;
	uint32_t data_sz;
	
	// パラメータチェック
	if (ch >= USART_CH_MAX) {
		return -1;
	}
	if (p_data == NULL) {
		return -1;
	}
	
	// 制御ブロック取得
	this = get_myself(ch);
	
	// オープン状態でなければ終了
	if (this->status != ST_OPEN) {
		return -1;
	}
	
	// 割り込み禁止
	__disable_irq();
	
	// リングバッファ情報取得
	p_ring_buf = &(this->rcv_buf);
	
	// リングバッファに入っているデータ数を取得
	data_sz = ((p_ring_buf->w_idx - p_ring_buf->r_idx) & (BUFF_SIZE - 1));
	// 受信サイズ更新
	// (*) リングバッファに入っているデータ数以上はいらない
	if (data_sz > size) {
		data_sz = size;
	}
	recv_sz = data_sz;
	
	// とりあえずリングバッファからとってくる
	while (data_sz-- > 0) {
		// リングバッファからデータを取得
		*(p_data++) = p_ring_buf->data[p_ring_buf->r_idx];
		p_ring_buf->r_idx = ((p_ring_buf->r_idx + 1) & (BUFF_SIZE - 1));
	}
	
	// 割り込み禁止解除
	__enable_irq();
	
	return recv_sz;
}
