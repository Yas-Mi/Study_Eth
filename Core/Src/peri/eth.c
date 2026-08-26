/*
 * eth.c
 *
 *	Created on: 2025/9/8
 *		Author: user
 */
#include <string.h>
#include <stdlib.h>
#include "stm32f7xx.h"
#include "stm32f7xx_hal_rcc.h"
#include "cmsis_os.h"
#include "iodefine.h"
#include "console.h"

#include "eth.h"

// マクロ
#define DATA_BUFF_SIZE_MAX		(1536)
#define BUFF_SISE_K				(64)
#define PHY_ADDRESS				(0)
#define RETRY_MAX				(10)

// 機能マクロ
#define MMC_ENABLE
//#define LOOPBACK_TEST_ENABLE
#define WAKEUP_ENABLE
#define PTP_ENABLE

// 状態定義
#define ST_INIT		(0)		// 初期状態
#define ST_CLOSE	(1)		// クローズ状態
#define ST_OPEN		(2)		// オープン状態
#define ST_MAX		(3)		// 最大値

// 送信完了イベント
#define EVT_SEND_SUCCESS	(1UL << 0)
#define EVT_RECV_SUCCESS	(1UL << 1)
#define EVT_SEND_FAIL		(1UL << 2)
#define EVT_RECV_FAIL		(1UL << 3)

// 送信ディスクリプタ
#define TDES0_OWN		(1 << 31)
#define TDES0_IC		(1 << 30)
#define TDES0_LS		(1 << 29)
#define TDES0_FS		(1 << 28)
#define TDES0_DC		(1 << 27)
#define TDES0_DP		(1 << 26)
#define TDES0_TTSE		(1 << 25)
#define TDES0_CIC		(((v) & 0x3) << 22)
#define TDES0_TER		(1 << 21)
#define TDES0_TCH		(1 << 20)
#define TDES0_TTSS		(1 << 17)
#define TDES0_IHE		(1 << 16)
#define TDES0_ES		(1 << 15)
#define TDES0_JT		(1 << 14)
#define TDES0_FF		(1 << 13)
#define TDES0_IPE		(1 << 12)
#define TDES0_LCA		(1 << 11)
#define TDES0_NC		(1 << 10)
#define TDES0_LCO		(1 << 9)
#define TDES0_EC		(1 << 8)
#define TDES0_VF		(1 << 7)
#define TDES0_CC		(((v) & 0xF) << 3)
#define TDES0_ED		(1 << 2)
#define TDES0_UF		(1 << 1)
#define TDES0_DB		(1 << 0)
#define TDES1_TBS1(v)	(((v) & 0x0FFF) << 0)
#define TDES1_TBS2(v)	(((v) & 0x0FFF) << 16)

// 受信ディスクリプタ
#define RDES0_OWN		(1 << 31)
#define RDES0_AFM		(1 << 30)
#define RDES0_FL(v)		(((v) & 0x3FFF) << 16)
#define RDES0_ES		(1 << 15)
#define RDES0_DE		(1 << 14)
#define RDES0_SAF		(1 << 13)
#define RDES0_LE		(1 << 12)
#define RDES0_OE		(1 << 11)
#define RDES0_VLAN		(1 << 10)
#define RDES0_FS		(1 << 9)
#define RDES0_LS		(1 << 8)
#define RDES0_IPHCE_TSV	(1 << 7)
#define RDES0_LCO		(1 << 6)
#define RDES0_FT		(1 << 5)
#define RDES0_RWT		(1 << 4)
#define RDES0_RE		(1 << 3)
#define RDES0_DBE		(1 << 2)
#define RDES0_CE		(1 << 1)
#define RDES0_PCE_ESA	(1 << 0)
#define RDES1_DIC		(1 << 31)
#define RDES1_RBS2(v)	(((v) & 0x1FFF) << 16)
#define RDES1_PER		(1 << 15)
#define RDES1_RCH		(1 << 14)
#define RDES1_RBS(v)	(((v) & 0x1FFF) << 0)
#define GET_RDES0_FL(v)	(((v) & (0x3FFF << 16)) >> 16)

// PHYレジスタ
#define PHY_REG_BASIC_CONTROL										(0)
#define PHY_REG_BASIC_STATUS										(1)
#define PHY_REG_PHY_IDENTIFIER_1									(2)
#define PHY_REG_PHY_IDENTIFIER_2									(3)
#define PHY_REG_AUTO_NEG_ADVERTISEMENT								(4)
#define PHY_REG_AUTO_NEG_LINK_PARTNER_ABILITY						(5)
#define PHY_REG_AUTO_NEG_EXPANSION									(6)
#define PHY_REG_AUTO_NEG_NEXT_PAGE_TX								(7)
#define PHY_REG_AUTO_NEG_NEXT_PAGE_RX								(8)
#define PHY_REG_MMD_ACCESS_CONTROL									(13)
#define PHY_REG_MMD_ACCESS_ADDRESS_DATA								(14)
#define PHY_REG_EDPD_NLP_CROSSOVER_TIME								(16)
#define PHY_REG_MODE_CONTROL_STATUS									(17)
#define PHY_REG_SPECIAL_MODES										(18)
#define PHY_REG_TDR_PATTERNS_DELAY_CONTROL							(24)
#define PHY_REG_TDR_CONTROL_STATUS									(25)
#define PHY_REG_SYMBOL_ERROR_COUNTER								(26)
#define PHY_REG_SPECIAL_CONTROL_STATUS_INDICATION					(27)
#define PHY_REG_CABLE_LENGTH										(28)
#define PHY_REG_INTERRUPT_SOURCE_FLAG								(29)
#define PHY_REG_INTERRUPT_MASK										(30)
#define PHY_REG_PHY_SPECIAL_CONTROL_STATUS							(31)

// BASIC_CONTROL
#define BASIC_CONTROL_SOFT_RESET									(1 << 15)
#define BASIC_CONTROL_LOOP_BACK										(1 << 14)
#define BASIC_CONTROL_SPEED_SELECT_10MBPS							(0 << 13)
#define BASIC_CONTROL_SPEED_SELECT_100MBPS							(1 << 13)
#define BASIC_CONTROL_POWER_DOWN									(1 << 11)
#define BASIC_CONTROL_ISOLATE										(1 << 10)
#define BASIC_CONTROL_RESTART_AUTO_NEGOTIATE						(1 << 9)
#define BASIC_CONTROL_DUPLEX										(1 << 8)

// BASIC STATUS
#define BASIC_STAUS_100BASE_T4										(1 << 15)
#define BASIC_STAUS_100BASE_TX_FULL_DUPLEX							(1 << 14)
#define BASIC_STAUS_100BASE_TX_HALF_DUPLEX							(1 << 13)
#define BASIC_STAUS_10BASE_T_FULL_DUPLEX							(1 << 12)
#define BASIC_STAUS_10BASE_T_HALF_DUPLEX							(1 << 11)
#define BASIC_STAUS_100BASE_T2_FULL_DUPLEX							(1 << 10)
#define BASIC_STAUS_100BASE_T2_HALF_DUPLEX							(1 << 9)
#define BASIC_STAUS_EXTENDED_STATUS									(1 << 8)
#define BASIC_STAUS_AUTO_NEGOTIATE_COMPLETE							(1 << 5)
#define BASIC_STAUS_REMOTE_FAULT									(1 << 4)
#define BASIC_STAUS_AUTO_NEGOTIATE_ABILITY							(1 << 3)
#define BASIC_STAUS_LINK_STATUS										(1 << 2)
#define BASIC_STAUS_JABBER_DETECT									(1 << 1)
#define BASIC_STAUS_EXTENDED_CAPABLITIES							(1 << 0)

// AUTO_NEG_ADVERTISEMENT
#define AUTO_NEG_ADVERTISEMENT_NEXT_PAGE							(1 << 15)
#define AUTO_NEG_ADVERTISEMENT_REMOTE_FAULT							(1 << 13)
#define AUTO_NEG_ADVERTISEMENT_PAUSE_OPERATION(v)					(((v) & 0x3) << 10)
#define AUTO_NEG_ADVERTISEMENT_100BASE_TX_FULL_DUPLEX				(1 << 8)
#define AUTO_NEG_ADVERTISEMENT_100BASE_TX							(1 << 7)
#define AUTO_NEG_ADVERTISEMENT_10BASE_T_FULL_DUPLEX					(1 << 6)
#define AUTO_NEG_ADVERTISEMENT_10BASE_TX							(1 << 5)

// AUTO_NEG_LINK_PARTNER_ABILITY
#define AUTO_NEG_LINK_PARTNER_ABILITY_NEXT_PAGE						(1 << 15)
#define AUTO_NEG_LINK_PARTNER_ABILITY_REMOTE_FAULT					(1 << 13)
#define AUTO_NEG_LINK_PARTNER_ABILITY_PAUSE_OPERATION(v)			(((v) & 0x3) << 10)
#define AUTO_NEG_LINK_PARTNER_ABILITY_100BASE_T4					(1 << 9)
#define AUTO_NEG_LINK_PARTNER_ABILITY_100BASE_TX_FULL_DUPLEX		(1 << 8)
#define AUTO_NEG_LINK_PARTNER_ABILITY_100BASE_TX					(1 << 7)
#define AUTO_NEG_LINK_PARTNER_ABILITY_10BASE_T_FULL_DUPLEX			(1 << 6)
#define AUTO_NEG_LINK_PARTNER_ABILITY_10BASE_T						(1 << 5)

// AUTO_NEG_EXPANSION
#define AUTO_NEG_EXPANSION_RECEIVE_NEXT_PAGE_LOCATION_ABLE			(1 << 6)
#define AUTO_NEG_EXPANSION_RECEIVED_NEXT_PAGE_STORAGE_LOCATION		(1 << 5)
#define AUTO_NEG_EXPANSION_PARALLEL_DETECTION_FAULT					(1 << 4)
#define AUTO_NEG_EXPANSION_LINK_PARTNER_NEXT_PAGE_ABLE				(1 << 3)
#define AUTO_NEG_EXPANSION_NEXT_PAGE_ABLE							(1 << 2)
#define AUTO_NEG_EXPANSION_PAGE_RECEIVED							(1 << 1)
#define AUTO_NEG_EXPANSION_LINK_PARTNER_AUTO_NEG_ABLE				(1 << 0)

// AUTO_NEG_NEXT_PAGE_TX
#define AUTO_NEG_NEXT_PAGE_TX_NEXT_PAGE								(1 << 15)
#define AUTO_NEG_NEXT_PAGE_TX_MESSAGE_PAGE							(1 << 13)
#define AUTO_NEG_NEXT_PAGE_TX_ACKNOELEDGE_2							(1 << 12)
#define AUTO_NEG_NEXT_PAGE_TX_TOGGLE								(1 << 11)
#define AUTO_NEG_NEXT_PAGE_TX_MESSAGE_CODE							(0x2FF << 0)

// AUTO_NEG_NEXT_PAGE_RX
#define AUTO_NEG_NEXT_PAGE_RX_NEXT_PAGE								(1 << 15)
#define AUTO_NEG_NEXT_PAGE_RX_MESSAGE_PAGE							(1 << 13)
#define AUTO_NEG_NEXT_PAGE_RX_ACKNOELEDGE_2							(1 << 12)
#define AUTO_NEG_NEXT_PAGE_RX_TOGGLE								(1 << 11)
#define AUTO_NEG_NEXT_PAGE_RX_MESSAGE_CODE							(0x2FF << 0)

// EDPD_NLP_CROSSOVER_TIME
#define EDPD_NLP_CROSSOVER_TIME_TX_NLP_ENABLE						(1 << 15)
#define EDPD_NLP_CROSSOVER_TIME_TX_NLP_INTERVAL_TIMER_SELECT		(0x3 << 14)
#define EDPD_NLP_CROSSOVER_TIME_RX_SINGLE_NLP_WAKE_ENABLE			(1 << 12)
#define EDPD_NLP_CROSSOVER_TIME_RX_NLP_MAX_INTERVAL_DETECT_SELECT	(0x3 << 14)

// MMD
#define MMD_DEVICE_ADDRESS_PCS										(3)
#define MMD_DEVICE_ADDRESS_VENDOR									(30)

// MMD_ACCESS_CONTROL
#define MMD_FUNCTION_ADDRESS										(0)
#define MMD_FUNCTION_DATA											(1)
#define MMD_ACCESS_CONTROL_MMD_FUNCTION(v)							(((v) & 0x3) << 14)
#define MMD_ACCESS_CONTROL_MMD_DEVICE_ADDRESS(v)					(((v) & 0x1F) << 0)

// MMDレジスタ
#define MMD_REG_PCS_MMD_DEVICE_PRESENT_1							(5)
#define MMD_REG_PCS_MMD_DEVICE_PRESENT_2							(6)
#define MMD_REG_WAKEUP_CONTROL_STATUS								(32784)
#define MMD_REG_WAKEUP_FILTER_CONFIG_A								(32785)
#define MMD_REG_WAKEUP_FILTER_CONFIG_B								(32786)
#define MMD_REG_WAKEUP_FILTER_BYTE_MASK								(32801)
#define MMD_REG_MAC_RECEIVE_ADDRESS_A								(32865)
#define MMD_REG_MAC_RECEIVE_ADDRESS_B								(32866)
#define MMD_REG_MAC_RECEIVE_ADDRESS_C								(32867)
#define MMD_REG_VENDOR_SPECIFIC_MMD1_DEVICEID_1						(2)
#define MMD_REG_VENDOR_SPECIFIC_MMD1_DEVICEID_2						(3)
#define MMD_REG_VENDOR_SPECIFIC1_MMD_DEVICE_PRESENT_1				(5)
#define MMD_REG_VENDOR_SPECIFIC1_MMD_DEVICE_PRESENT_2				(6)
#define MMD_REG_VENDOR_SPECIFIC_MMD1_STATUS							(8)
#define MMD_REG_TDR_MATCH_THRESHOLD									(11)
#define MMD_REG_TDR_SHORT_OPEN_THRESHOLD							(12)
#define MMD_REG_VENDOR_SPECIFIC_MMD1_PACKAGEID_1					(14)
#define MMD_REG_VENDOR_SPECIFIC_MMD1_PACKAGEID_2					(15)

// LED
typedef enum {
	LED_IDX_1 = 0,
	LED_IDX_2,
	LED_IDX_MAX,
} LED_IDX;
typedef enum {
	LDE_FUNC_LINK_ACTIVITY = 0,
	LDE_FUNC_nINT,
	LDE_FUNC_nPME,
	LDE_FUNC_LINK_SPEED,
	LED_FUNC_MAX,
}LED_FUNC;

// レジスタ設定マクロ
#define MACMIIAR_PA(v)							(((v) & 0x1F) << ETH_MACMIIAR_PA_Pos)	// 
#define MACMIIAR_MR(v)							(((v) & 0x1F) << ETH_MACMIIAR_MR_Pos)	// 
#define MACMIIAR_CR(v)							(((v) & 0x7)  << ETH_MACMIIAR_CR_Pos)	// 
#define WUCSR_LED_FUNCTION_SELECT(idx,func)		(idx == LED_IDX_1) ? (((func) & 0x3)  << 13) : (((func) & 0x3)	<< 11)

// チャネル情報
typedef struct {
	ETH_TypeDef		*p_reg;			// ベースレジスタ
	IRQn_Type		global_irqn;	// 割り込み番号(グローバル)
	IRQn_Type		wakeup_irqn;	// 割り込み番号(ウェイクアップ)
	uint32_t		priority;		// 優先度
} CH_INFO;
static const CH_INFO ch_info_tbl = {
	ETH,	
	ETH_IRQn,	
	ETH_WKUP_IRQn,	
	5
};

// MACアドレス
static const uint8_t first_mac_address[6] = {
	0x00, 0x11, 0x22, 0x33, 0x44, 0x55
};

// MACアドレス
static const uint8_t second_mac_address[6] = {
	0x55, 0x44, 0x33, 0x22, 0x11, 0x00
};

// MMDレジスタ情報
typedef struct {
	uint16_t	index;
	uint16_t	addr;
} MMD_INFO;
static const MMD_INFO mmd_info_tbl[] = {
	{MMD_REG_PCS_MMD_DEVICE_PRESENT_1,				MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_PCS_MMD_DEVICE_PRESENT_2,				MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_WAKEUP_CONTROL_STATUS,					MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_WAKEUP_FILTER_CONFIG_A,				MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_WAKEUP_FILTER_CONFIG_B,				MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_WAKEUP_FILTER_BYTE_MASK,				MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_MAC_RECEIVE_ADDRESS_A,					MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_MAC_RECEIVE_ADDRESS_B,					MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_MAC_RECEIVE_ADDRESS_C,					MMD_DEVICE_ADDRESS_PCS},
	{MMD_REG_VENDOR_SPECIFIC_MMD1_DEVICEID_1,		MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_VENDOR_SPECIFIC_MMD1_DEVICEID_2,		MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_VENDOR_SPECIFIC1_MMD_DEVICE_PRESENT_1,	MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_VENDOR_SPECIFIC1_MMD_DEVICE_PRESENT_2,	MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_VENDOR_SPECIFIC_MMD1_STATUS,			MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_TDR_MATCH_THRESHOLD,					MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_TDR_SHORT_OPEN_THRESHOLD,				MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_VENDOR_SPECIFIC_MMD1_PACKAGEID_1,		MMD_DEVICE_ADDRESS_VENDOR},
	{MMD_REG_VENDOR_SPECIFIC_MMD1_PACKAGEID_2,		MMD_DEVICE_ADDRESS_VENDOR},
};

// ディスクリプタはペリフェラルドライバで持つ
typedef union {
	uint32_t DES[4];
	struct {
		uint32_t TDES0;
		uint32_t TDES1;
		uint32_t TDES2;
		uint32_t TDES3;
	} tx;
	struct {
		uint32_t RDES0;
		uint32_t RDES1;
		uint32_t RDES2;
		uint32_t RDES3;
	} rx;
} DESCRIPTOR;
static DESCRIPTOR tx_descriptor[TX_DISCRIPTOR_NUM] __ALIGNED(32);
static DESCRIPTOR rx_descriptor[RX_DISCRIPTOR_NUM] __ALIGNED(32);

// インデックス計算マクロ(*)numは2の乗数にすること
#define INC_DESC_IDX(num,cur_idx) (cur_idx = ((cur_idx + 1) & (num - 1)))

// 受信バッファ
static uint8_t rx_buff[RX_DISCRIPTOR_NUM][DATA_BUFF_SIZE_MAX] __ALIGNED(32);
// 送信バッファ
static uint8_t tx_buff[TX_DISCRIPTOR_NUM][DATA_BUFF_SIZE_MAX] __ALIGNED(32);

// ディスクリプタ情報
typedef struct {
	uint32_t	cur_idx;	// 現在のディスクリプタ位置
	DESCRIPTOR	*p_desc;	// ディスクリプタのポインタ
} DESCRIPTOR_INFO;

// 制御ブロック
typedef struct {
	uint32_t			status;			// 状態
	osMailQId			mail_handle;	// メールハンドル
	osThreadId			snd_thread_id;	// タスクID
	osThreadId			rcv_thread_id;	// タスクID
	DESCRIPTOR_INFO		tx_desc_info;	// 送信ディスクリプタ情報
	DESCRIPTOR_INFO		rx_desc_info;	// 受信ディスクリプタ情報
	ETH_TX_CALLBACK		tx_cb;			// 送信コールバック
	ETH_RX_CALLBACK		rx_cb;			// 受信コールバック
	ETH_ERR_CALLBACK	err_cb;			// エラーコールバック
	void *				cb_vp;			// 送信コールバックパラメータ
} ETH_CB;
static ETH_CB eth_cb[ETH_CH_MAX];
#define get_myself(ch) (&eth_cb[ch])

// 割り込みハンドラ
void ETH_IRQHandler(void)
{
	ETH_CB *this = get_myself(ETH_CH_1);
	ETH_TypeDef *p_reg;
	uint16_t macsr;
	uint32_t dmasr;
	uint32_t dmaier;
	uint32_t exti_pr;
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// 各レジスタの値を取得
	macsr = p_reg->MACSR;
	dmasr = p_reg->DMASR;
	dmaier = p_reg->DMAIER;
	exti_pr = EXTI->PR;
	
	// エラー確認
	if ((dmasr & ETH_DMASR_EBS) != 0) {
		// エラーコールバック
		if (this->err_cb != NULL) {
			this->err_cb(this->cb_vp, 0);
		}
		return;
		
	}
	
	// 受信完了
	if (((dmaier & ETH_DMAIER_RIE) != 0) && ((dmasr & ETH_DMASR_RS) != 0)) {
		// フラグクリア
		p_reg->DMASR |= (ETH_DMASR_NIS | ETH_DMASR_RS);
		// 受信コールバック
		if (this->rx_cb != NULL) {
			this->rx_cb(this->cb_vp, 0);
		}
		
	// 送信完了
	} else if (((dmaier & ETH_DMAIER_TIE) != 0) && ((dmasr & ETH_DMASR_TS) != 0)) {
		// フラグクリア
		p_reg->DMASR |= (ETH_DMASR_NIS | ETH_DMASR_TS | ETH_DMASR_TBUS);
		// 送信コールバック
		if (this->tx_cb != NULL) {
			this->tx_cb(this->cb_vp, 0);
		}
		
	// WakeUp
	} else if ((exti_pr & ETH_WAKEUP_EXTI_LINE) != 0) {
		/* Clear ETH WAKEUP Exti pending bit */
		__HAL_ETH_WAKEUP_EXTI_CLEAR_FLAG(ETH_WAKEUP_EXTI_LINE);
		
	} else {
		;
		
	}
}

/**
  * @brief This function handles Ethernet wake-up interrupt through EXTI line 19.
  */
void ETH_WKUP_IRQHandler(void)
{
	/* Clear ETH WAKEUP Exti pending bit */
	__HAL_ETH_WAKEUP_EXTI_CLEAR_FLAG(ETH_WAKEUP_EXTI_LINE);
}

// DMAリセット
static void dma_reset(ETH_TypeDef *p_reg)
{
	uint32_t timeout = 1000;
	
	p_reg->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
	p_reg->DMAOMR &= ~(ETH_DMAOMR_ST | ETH_DMAOMR_SR);
	p_reg->DMABMR |= ETH_DMABMR_SR;
	while ((p_reg->DMABMR & ETH_DMABMR_SR) != 0) {
		if (--timeout == 0) {
			break;	// SR が読めない errata 対策
		}
	}
}

// レジスタ設定
static void eth_config(ETH_TypeDef *p_reg, ETH_OPEN *p_par)
{
	uint32_t loopback_setting = 0;
	volatile uint32_t tmp_reg;
	
	// クロック有効
	__HAL_RCC_SYSCFG_CLK_ENABLE();
	
	  /* Select MII or RMII Mode*/
	SYSCFG->PMC &= ~(SYSCFG_PMC_MII_RMII_SEL);
	SYSCFG->PMC |= (uint32_t)HAL_ETH_RMII_MODE;	
	
	// DMAリセット
	dma_reset(p_reg);
	
	// ディスクリプタのアドレスを設定
	p_reg->DMATDLAR = (uint32_t)&(tx_descriptor[0]);
	p_reg->DMARDLAR = (uint32_t)&(rx_descriptor[0]);
	
	// DMA設定
	// Tx FIFO : 256 bytes
	// Rx FIFO : 128 bytes
	// バースト長は16word(16*4=64byte)
	p_reg->DMABMR |= (ETH_DMABMR_FB | ETH_DMABMR_AAB | ETH_DMABMR_PBL_16Beat);
	tmp_reg = p_reg->DMABMR;
	// ディレイ
	osDelay(1);
	
	// ループバック設定
#ifdef LOOPBACK_TEST_ENABLE
	// ROD(1)  : Receive own 有効 → 
	loopback_setting = ETH_MACCR_LM;
#endif
	
	// レジスタ設定
	// CSTF(1) : CRC stripping有効 → FCSを削除してバッファに格納
	// WD(0)   : ウォッチドッグ有効 → 2048byte以降は切り捨てる
	// JD(0)   : Jabber 有効 → 2048byte以上送信しようとすると
	// IPCO(1) : - IPヘッダー、TCP/UDPチェックサムを自動検証
	// APCS(1) : - パディング領域とFCSを自動的に除去
	//p_reg->MACCR = ETH_MACCR_CSTF | ETH_MACCR_DM | ETH_MACCR_IPCO | ETH_MACCR_APCS | loopback_setting;
	p_reg->MACCR = ETH_MACCR_FES | ETH_MACCR_DM | ETH_MACCR_IPCO | ETH_MACCR_APCS | loopback_setting;
	//p_reg->MACCR = ETH_MACCR_DM | ETH_MACCR_IPCO | ETH_MACCR_APCS | loopback_setting;
	// ※ ETH_MACCR_FESを設定することでちゃんと送受信できるようになった
	
	// フィルタレジスタ設定
	// HPF(0)  : MACアドレスレジスタと完全一致する場合のみ受信※1の場合は、ハッシュ
	// SAF(0)  : 送信元MACアドレスはチェックされず、通常の受信処理
	// SAIF(0) : 通常のSAF動作（送信元MACが一致すれば受信）
	// PCF(0)  : 制御フレームを破棄
	// BFD(0)  : ブロードキャストフレームを受信
	// PAM(0)  : マルチキャストアドレスは ハッシュテーブルや完全一致フィルタで選別される
	// DAIF(0) : 宛先MACアドレスが MACA1HR/MACA1LR に一致すれば受信
	// HM(0)   : マルチキャストアドレスはハッシュフィルタされない
	// HU(0)   : ユニキャストアドレスは完全一致（Perfect Filter）でのみ受信
	// PM(0)   : - MACアドレスフィルタが有効。自分宛のフレームのみ受信
	//p_reg->MACFFR = 0;
	// プロミスキャストモード
	p_reg->MACFFR = ETH_MACFFR_PM;
	
	// ハッシュフィルタは使用しない
	p_reg->MACHTHR = 0;
	p_reg->MACHTLR = 0;
	
	// フロー制御
	// いったんデフォルト設定
	p_reg->MACFCR = 0;
	
	// VLAN設定
	// VLANはいったん使わない
	p_reg->MACVLANTR = 0;
	
	// ウェイクアップ設定
	// ウェイクアップはいったん使わない
	p_reg->MACRWUFFR = 0;
	p_reg->MACPMTCSR = 0;
	//p_reg->MACPMTCSR |= (ETH_MACPMTCSR_WFE | ETH_MACPMTCSR_MPE);
	
	// マックアドレス設定
#if 0
	p_reg->MACA0HR = (((uint32_t)first_mac_address[5] << 8) | ((uint32_t)first_mac_address[4] << 0));
	p_reg->MACA0LR = (((uint32_t)first_mac_address[3] << 24) | ((uint32_t)first_mac_address[2] << 16) | 
					   ((uint32_t)first_mac_address[1] << 8) | ((uint32_t)first_mac_address[0] << 0));
	p_reg->MACA1HR = (((uint32_t)second_mac_address[5] << 8) | ((uint32_t)second_mac_address[4] << 0));
	p_reg->MACA1LR = (((uint32_t)second_mac_address[3] << 24) | ((uint32_t)second_mac_address[2] << 16) | 
					   ((uint32_t)second_mac_address[1] << 8) | ((uint32_t)second_mac_address[0] << 0));
	p_reg->MACA1HR |= ETH_MACA1HR_AE;
#endif
	p_reg->MACA0HR = (((uint32_t)p_par->mac_addr[5] << 8) | ((uint32_t)p_par->mac_addr[4] << 0));
	p_reg->MACA0LR = (((uint32_t)p_par->mac_addr[3] << 24) | ((uint32_t)p_par->mac_addr[2] << 16) | 
					   ((uint32_t)p_par->mac_addr[1] << 8) | ((uint32_t)p_par->mac_addr[0] << 0));
	
	
	// MACA1~3LR、MACA1~3HRはいったん使用しない
	
#ifdef MMC_ENABLE
	// カウンタリセット
	p_reg->MMCCR |= ETH_MMCCR_CR;
	
	// 送受信割り込み有効
	p_reg->MMCRIR |= (ETH_MMCRIR_RGUFS | ETH_MMCRIR_RFAES | ETH_MMCRIR_RFCES);
	p_reg->MMCTIR |= (ETH_MMCTIR_TGFS | ETH_MMCTIR_TGFMSCS | ETH_MMCTIR_TGFSCS);
	
#endif

#ifdef PTP_ENABLE
	// 割り込みをマスク
	p_reg->MACIMR |= ETH_MACIMR_TSTIM;
	
	// タイムスタンプ機能を有効化
	p_reg->PTPTSCR |= 
	
	
	// タイムスタンプ機能は有効
	//	TSPFFMAE(1)	 : 受信フレームの宛先MACアドレスが一致する場合にタイムスタンプを生成
	//	TSSMRME(1)	 : マスター宛のメッセージに対してのみ受信時にタイムスタンプスナップショットを取得する
	//	TSSEME(1)	 : PTPイベントメッセージ（Sync, Delay_Req など）に対してのみ、タイムスタンプスナップショットを取得する
	//	TSSIPV4FE(1) : IPv4パケットに対してのみタイムスタンプスナップショットを取得
	p_reg->PTPTSCR |= (ETH_PTPTSCR_TSPFFMAE | ETH_PTPTSCR_TSSMRME | ETH_PTPTSCR_TSSEME | ETH_PTPTSCR_TSSIPV4FE | ETH_PTPTSCR_TSE);
	
#endif
	
	// 割り込み設定
	p_reg->DMAIER |= (ETH_DMAIER_NISE | ETH_DMAIER_AISE | ETH_DMAIER_RIE | ETH_DMAIER_TIE);
	
	// 送受信有効
	p_reg->MACCR |= (ETH_MACCR_TE | ETH_MACCR_RE);
	
	// 送信開始
	// → これでDMAがディスクリプタのOWNビットポーリングが始まる
	p_reg->DMAOMR |= ETH_DMAOMR_ST;
	
	// 受信開始
	p_reg->DMARPDR = 0;
	p_reg->DMAOMR |= ETH_DMAOMR_SR;
	
}

// 送信ディスクリプタ設定
static void tx_desc_config(ETH_CB *this)
{
	DESCRIPTOR_INFO *p_desc_info;
	DESCRIPTOR *p_cur_desc;
	DESCRIPTOR *p_nxt_desc;
	uint8_t i;
	
	// 送信ディスクリプタを制御ブロックにセット
	p_desc_info = &(this->tx_desc_info);
	p_desc_info->p_desc = tx_descriptor;
	
	// 送信ディスクリプタ設定
	for (i = 0; i < (TX_DISCRIPTOR_NUM - 1); i++) {
		// ディスクリプタ取得
		p_cur_desc = &(p_desc_info->p_desc[i]);
		// 次のディスクリプタのアドレスを取得
		p_nxt_desc = &(p_desc_info->p_desc[i + 1]);
		// 設定
		p_cur_desc->tx.TDES3 = (uint32_t)p_nxt_desc;
		p_cur_desc->tx.TDES2 = (uint32_t)&(tx_buff[i][0]);
	}
	// 最後のディスクリプタ設定
	p_cur_desc = &(p_desc_info->p_desc[TX_DISCRIPTOR_NUM - 1]);
	p_cur_desc->tx.TDES2 = (uint32_t)&(tx_buff[TX_DISCRIPTOR_NUM - 1][0]);
	p_cur_desc->tx.TDES3 = (uint32_t)&(p_desc_info->p_desc[0]);
}

// 受信ディスクリプタ設定
static void rx_desc_config(ETH_CB *this)
{
	DESCRIPTOR_INFO *p_desc_info;
	DESCRIPTOR *p_cur_desc;
	DESCRIPTOR *p_nxt_desc;
	uint8_t i;
	
	// 受信ディスクリプタを制御ブロックにセット
	p_desc_info = &(this->rx_desc_info);
	p_desc_info->p_desc = rx_descriptor;
	
	// 受信ディスクリプタ設定
	for (i = 0; i < RX_DISCRIPTOR_NUM; i++) {
		// ディスクリプタ取得
		p_cur_desc = &(p_desc_info->p_desc[i]);
		// 次のディスクリプタのアドレスを取得
		if (i != (RX_DISCRIPTOR_NUM - 1)) {
			p_nxt_desc = &(p_desc_info->p_desc[i + 1]);
		} else {
			p_nxt_desc = &(p_desc_info->p_desc[0]);
		}
		// 設定
		p_cur_desc->rx.RDES3 = (uint32_t)p_nxt_desc;
		// 各設定 (*)チェインモード
		p_cur_desc->rx.RDES0 |= RDES0_OWN;
		p_cur_desc->rx.RDES1 |= RDES1_RBS(DATA_BUFF_SIZE_MAX) | RDES1_RCH;
		p_cur_desc->rx.RDES1 &= ~RDES1_DIC;
		p_cur_desc->rx.RDES2 = (uint32_t)&(rx_buff[i][0]);
	}
}

// ディスクリプタ設定
static void desc_config(ETH_CB *this)
{
	tx_desc_config(this);
	rx_desc_config(this);
}

// PHYレジスタ読み出し
static osStatus phy_read(ETH_TypeDef *p_reg, uint8_t phy_reg, uint16_t *data)
{
	osStatus ercd = osErrorTimeoutResource;
	uint16_t timeout = 1000;
	
	// PHYアドレスとリードしたいレジスタのインデックスを設定
	p_reg->MACMIIAR = MACMIIAR_PA(PHY_ADDRESS) | MACMIIAR_MR(phy_reg);
	
	// クロックはデフォルト設定
	p_reg->MACMIIAR |= MACMIIAR_CR(0);
	
	// 読みとり開始
	p_reg->MACMIIAR |= ETH_MACMIIAR_MB;
	
	// 読み出しが終わるまで待つ
	while (timeout--) {
		if ((p_reg->MACMIIAR & ETH_MACMIIAR_MB) == 0) {
			ercd = osOK;
			break;
		}
	}
	
	// 読み出し
	*data = p_reg->MACMIIDR;
	
	return ercd;
}

// PHYレジスタ書き込み
static osStatus phy_write(ETH_TypeDef *p_reg, uint8_t phy_reg, uint16_t data)
{
	osStatus ercd = osErrorTimeoutResource;
	uint16_t timeout = 1000;
	
	// 書き込み
	p_reg->MACMIIDR = data;
	
	// 書き込み開始
	p_reg->MACMIIAR = MACMIIAR_PA(PHY_ADDRESS) | MACMIIAR_MR(phy_reg) | ETH_MACMIIAR_MW | ETH_MACMIIAR_MB;
	
	// 書き込みが終わるまで待つ
	while (timeout--) {
		if ((p_reg->MACMIIAR & ETH_MACMIIAR_MB) == 0) {
			ercd = osOK;
			break;
		}
	}
	
	return ercd;
}

// MMDレジスタ読み出し
static osStatus mmd_read(ETH_TypeDef *p_reg, uint16_t index, uint16_t *data)
{
	osStatus ercd;
	uint16_t set_val = 0;
	uint8_t i;
	
	// アドレス取得
	for (i = 0; i < sizeof(mmd_info_tbl)/sizeof(mmd_info_tbl[0]); i++) {
		if (mmd_info_tbl[i].index == index) {
			break;
		}
	}
	if (i >= sizeof(mmd_info_tbl)/sizeof(mmd_info_tbl[0])) {
		ercd = osErrorResource;
		goto EXIT;
	}
	
	// アクセス制御レジスタ書き込み
	set_val = mmd_info_tbl[i].addr;
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_CONTROL, set_val)) != osOK) {
		goto EXIT;
	}
	
	// アクセス/データレジスタ書き込み
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_ADDRESS_DATA, index)) != osOK) {
		goto EXIT;
	}
	
	// アクセス制御レジスタ書き込み
	set_val |= MMD_ACCESS_CONTROL_MMD_FUNCTION(MMD_FUNCTION_DATA);
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_CONTROL, set_val)) != osOK) {
		goto EXIT;
	}
	
	// アクセス/データレジスタ読み込み
	if ((ercd = phy_read(p_reg, PHY_REG_MMD_ACCESS_ADDRESS_DATA, data)) != osOK) {
		goto EXIT;
	}
	
EXIT:
	return ercd;
}

// MMDレジスタ読み出し
static osStatus mmd_write(ETH_TypeDef *p_reg, uint16_t index, uint16_t data)
{
	osStatus ercd;
	uint16_t set_val = 0;
	uint8_t i;
	
	// アドレス取得
	for (i = 0; i < sizeof(mmd_info_tbl)/sizeof(mmd_info_tbl[0]); i++) {
		if (mmd_info_tbl[i].index == index) {
			break;
		}
	}
	if (i >= sizeof(mmd_info_tbl)/sizeof(mmd_info_tbl[0])) {
		ercd = osErrorResource;
		goto EXIT;
	}
	
	// アクセス制御レジスタ書き込み
	set_val = mmd_info_tbl[i].addr;
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_CONTROL, set_val)) != osOK) {
		goto EXIT;
	}
	
	// アクセス/データレジスタ書き込み
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_ADDRESS_DATA, index)) != osOK) {
		goto EXIT;
	}
	
	// アクセス制御レジスタ書き込み
	set_val |= MMD_ACCESS_CONTROL_MMD_FUNCTION(MMD_FUNCTION_DATA);
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_CONTROL, set_val)) != osOK) {
		goto EXIT;
	}
	
	// アクセス/データレジスタ書き込み
	if ((ercd = phy_write(p_reg, PHY_REG_MMD_ACCESS_ADDRESS_DATA, data)) != osOK) {
		goto EXIT;
	}
	
EXIT:
	return ercd;
}

// LED機能選択
static osStatus set_led_func(ETH_TypeDef *p_reg, LED_IDX idx, LED_FUNC func)
{
	uint16_t set_val = 0;
	osStatus ercd;
	
	// 設定値
	set_val = WUCSR_LED_FUNCTION_SELECT(idx, func);
	
	// 書き込み
	if ((ercd = mmd_write(p_reg, MMD_REG_WAKEUP_CONTROL_STATUS, set_val)) != osOK) {
		goto EXIT;
	}
	
EXIT:
	return ercd;
}

// 初期化
void eth_init(void)
{
	ETH_CB *this = get_myself(ETH_CH_1);
	
	// コンテキストクリア
	memset(this, 0, sizeof(ETH_CB));
	
	// ディスクリプタクリア
	memset(&tx_descriptor[0], 0, sizeof(DESCRIPTOR)*TX_DISCRIPTOR_NUM);
	memset(&rx_descriptor[0], 0, sizeof(DESCRIPTOR)*RX_DISCRIPTOR_NUM);
	
	// バッファクリア
	memset(rx_buff, 0, RX_DISCRIPTOR_NUM*DATA_BUFF_SIZE_MAX);
	
	// メールキュー作成 (*) 64*1024byteのメモリを確保
	osMailQDef(ConsoleSendBuf, BUFF_SISE_K, 1024);
	this->mail_handle = osMailCreate(osMailQ(ConsoleSendBuf), NULL);
	
	// 状態更新
	this->status = ST_CLOSE;
	
}

// オープン
osStatus eth_open(ETH_CH ch, ETH_OPEN *p_par)
{
	ETH_CB *this = get_myself(ETH_CH_1);
	ETH_TypeDef *p_reg;
	
	// パラメータチェック
	if (p_par == NULL) {
		return osErrorParameter;
	}
	
	// 初期化していない場合はエラー
	if (this->status != ST_CLOSE) {
		return osErrorResource;
	}
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// レジスタ設定
	eth_config(p_reg, p_par);
	
	// ディスクリプタ設定
	desc_config(this);
	
	// コールバック設定
	this->tx_cb = p_par->tx_cb;
	this->rx_cb = p_par->rx_cb;
	this->err_cb = p_par->err_cb;
	this->cb_vp = p_par->cb_vp;
	
	// 状態更新
	this->status = ST_OPEN;
	
	return osOK;
}

// 送信
extern void clean_dcache(void *addr, uint32_t size);
osStatus eth_send(ETH_CH ch, uint8_t *p_data, uint32_t size)
{
	ETH_CB *this = get_myself(ch);
	ETH_TypeDef *p_reg;
	DESCRIPTOR_INFO *p_info;
	DESCRIPTOR *p_desc;
	uint32_t	cur_idx;
	
	// パラメータチェック
	if ((p_data == NULL) || ((size == 0) || (size >DATA_BUFF_SIZE_MAX))) {
		return osErrorParameter;
	}
	
	// オープンしていない場合はエラー
	if (this->status != ST_OPEN) {
		return osErrorResource;
	}
	
	// ディスクリプタ情報を取得
	p_info = &(this->tx_desc_info);
	cur_idx = p_info->cur_idx;
	p_desc = &(p_info->p_desc[cur_idx]);
	
	// DMA使用中ならエラー返して終了
	if ((p_desc->tx.TDES0 & TDES0_OWN) != 0) {
		return osErrorResource;
	}
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// TDES0, 1設定
	p_desc->tx.TDES1 = TDES1_TBS1(size);
	p_desc->tx.TDES0 = (TDES0_FS | TDES0_TCH | TDES0_LS | TDES0_IC);
	
	// データコピー
	memcpy(&(tx_buff[cur_idx][0]), p_data, size);
	
	// データフラッシュ
	clean_dcache(&(tx_buff[cur_idx][0]), size);
	
	// ちゃんとデータがメモリに書かれてから以降の処理を実施
	__DMB();
	
	// DMAに処理を渡す
	p_desc->tx.TDES0 |= TDES0_OWN;
	
	// ディスクリプタ領域をフラッシュ
	clean_dcache(p_desc, sizeof(DESCRIPTOR)*TX_DISCRIPTOR_NUM);
		
	// TBUSクリア
	// STセット⇒OWNセットの流れの場合、STセットしたタイミングでDMAはOWNがセットされているディスクリプタを探しにいく。
	// ただし、OWNはまだセットされていないため必ずTBUSが立ってしまう
	p_reg->DMASR = (ETH_DMASR_TBUS | ETH_DMASR_TUS | ETH_DMASR_TPS | ETH_DMASR_AIS | ETH_DMASR_NIS);
	// 再度ディスクリプタチェック要求
	p_reg->DMATPDR = 0U;
	
	// ディスクリプタのインデックスを進める
	INC_DESC_IDX(TX_DISCRIPTOR_NUM, p_info->cur_idx);
	
	return osOK;
}

// 受信
// p_dataには1518byte以上の配列を渡してね
osStatus eth_recv(ETH_CH ch, uint8_t *p_data, uint32_t *p_size)
{
	ETH_CB *this = get_myself(ch);
	DESCRIPTOR_INFO *p_info;
	DESCRIPTOR *p_desc;
	uint16_t size;
	uint32_t	cur_idx;
	
	// パラメータチェック
	if ((p_data == NULL) || (p_size == NULL)) {
		return osErrorParameter;
	}
	
	// オープンしていない場合はエラー
	if (this->status != ST_OPEN) {
		return osErrorResource;
	}
	
	// ディスクリプタ情報を取得
	p_info = &(this->rx_desc_info);
	cur_idx = p_info->cur_idx;
	p_desc = &(p_info->p_desc[cur_idx]);
	
	// DMA使用中だったらエラー
	if ((p_desc->rx.RDES0 & RDES0_OWN) != 0) {
		return osErrorISR;
	}
	
	// 一応フレーム受信できているかチェック
	if (((p_desc->rx.RDES0 & RDES0_FS) == 0) || ((p_desc->rx.RDES0 & RDES0_LS) == 0)) {
		return osErrorISR;
	}
	
	// データコピー
	size = GET_RDES0_FL(p_desc->rx.RDES0);
	memcpy(p_data, (uint8_t *)(p_desc->rx.RDES2), size);
	*p_size = size;
	
	// ディスクリプタ返却
	p_desc->rx.RDES0 &= ~(RDES0_FS | RDES0_LS | RDES0_ES);
	p_desc->rx.RDES0 |= RDES0_OWN;
	
	// ディスクリプタのインデックスを進める
	INC_DESC_IDX(RX_DISCRIPTOR_NUM, p_info->cur_idx);
	
	return osOK;
}

// 停止
osStatus eth_go_down(void)
{
	ETH_CB *this = get_myself(ETH_CH_1);
	osStatus ercd;
	ETH_TypeDef *p_reg;
	DESCRIPTOR *p_cur_desc;
	uint8_t i;
	uint8_t own_cnt = 0;
	uint8_t retry_cnt = 0;
	const CH_INFO *p_ch_info = &ch_info_tbl;
	DESCRIPTOR_INFO *p_desc_info;
	
	// オープンしていない場合はエラー
	if (this->status != ST_OPEN) {
		return osErrorResource;
	}
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// 送信DMAを無効化
	p_reg->DMAOMR &= ~ETH_DMAOMR_ST;
	
	// 送受信停止
	p_reg->MACCR &= ~(ETH_MACCR_TE | ETH_MACCR_RE);
	
	// 受信ディスクリプタ情報を取得
	p_desc_info = &(this->rx_desc_info);
	
	// 受信FIFOの空確認 (※) 受信FIFOが空になっていることをどうやって判断するかは記載されていなかった
	//	OWNが1かどうかチェック（OWN=1の場合、DMA所有でFIFOにデータがない状態）
	while ((own_cnt != RX_DISCRIPTOR_NUM) && (retry_cnt < RETRY_MAX)) {
		// クリア
		own_cnt = 0;
		// 受信ディスクリプタ設定
		for (i = 0; i < RX_DISCRIPTOR_NUM; i++) {
			// ディスクリプタ取得
			p_cur_desc = &(p_desc_info->p_desc[i]);
			// OWNがセットされている
			if ((p_cur_desc->rx.RDES0 & RDES0_OWN) != 0) {
				own_cnt++;
			}
		}
		// リトライ回数を増やす
		retry_cnt++;
	}
	// リトライ回数やっても駄目だった
	if (retry_cnt >= RETRY_MAX) {
		ercd = osEventTimeout;
		goto EXIT;
	}
	
	// EXTI設定
	EXTI->IMR  |= EXTI_IMR_IM19;
	EXTI->EMR  |= EXTI_EMR_MR19;
	EXTI->RTSR |= EXTI_RTSR_TR19;
	EXTI->PR	= EXTI_PR_PR19;
	
	// 割り込み設定
	HAL_NVIC_SetPriority(p_ch_info->wakeup_irqn, 5, 0);
	HAL_NVIC_EnableIRQ(p_ch_info->wakeup_irqn);
	HAL_NVIC_ClearPendingIRQ(p_ch_info->wakeup_irqn);
	HAL_NVIC_DisableIRQ(p_ch_info->global_irqn);
	
	// 受信有効化
	p_reg->MACCR |= ETH_MACCR_RE;
	
	// Magic Paclet/Wake-on-Lan検出を有効化
	p_reg->MACPMTCSR |= (ETH_MACPMTCSR_WFE | ETH_MACPMTCSR_MPE | ETH_MACPMTCSR_GU);
	
	// パワーダウンモードを有効化
	p_reg->MACPMTCSR |= ETH_MACPMTCSR_PD;
	
	console_printf("EXTI->IMR:0x%x\n", EXTI->IMR);
	console_printf("EXTI->EMR:0x%x\n", EXTI->EMR);
	console_printf("EXTI->RTSR:0x%x\n", EXTI->RTSR);
	console_printf("EXTI->PR:0x%x\n", EXTI->PR);
	console_printf("MACPMTCSR:0x%x\n", p_reg->MACPMTCSR);
	
	osDelay(1000);
	
	// SysTickを中断 (※)中断しないと速攻で起きてしまう
	HAL_SuspendTick();
	
	// Stopモード以降
	//HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFE);
	HAL_PWR_EnterSLEEPMode(PWR_MAINREGULATOR_ON, PWR_SLEEPENTRY_WFE);
	
	// ★復帰後ここから始まる
	
	// クロック再初期化
	SystemClock_Config();
	
	// SysTick再開
	HAL_ResumeTick();
	
	console_printf("wakeup\n");
	
	// WakeUpソースをクリア
	p_reg->MACPMTCSR &= ~(ETH_MACPMTCSR_WFE | ETH_MACPMTCSR_MPE);
	
	// パワーダウンモード無効化
	p_reg->MACPMTCSR &= ~ETH_MACPMTCSR_PD;
	
EXIT:
	return ercd;
}

// コマンド
static void mmd_cmd_read(int argc, char *argv[])
{
	osStatus ercd;
	ETH_TypeDef *p_reg;
	uint16_t idx;
	uint16_t data;
	
	// 引数チェック
	if (argc < 2) {
		console_printf("mmd_cmd_read <idx>\n");
		return;
	}
	
	// 値設定
	idx = atoi(argv[1]);
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// 送信
	ercd = mmd_read(p_reg, idx, &data);
	console_printf("ercd:%d, data:%x\n", ercd, data);
}

// コマンド
static void mmd_cmd_write(int argc, char *argv[])
{
	osStatus ercd;
	ETH_TypeDef *p_reg;
	uint16_t idx;
	uint32_t data;
	
	// 引数チェック
	if (argc < 3) {
		console_printf("mmd_cmd_write <idx> <data>\n");
		return;
	}
	
	// 値設定
	idx = atoi(argv[1]);
	data = atoi(argv[2]);
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// 送信
	ercd = mmd_write(p_reg, idx, data);
	console_printf("ercd:%d, data:%x\n", ercd, data);
}

// コマンド
static void phy_cmd_read(int argc, char *argv[])
{
	osStatus ercd;
	ETH_TypeDef *p_reg;
	uint16_t idx;
	uint16_t data;
	
	// 引数チェック
	if (argc < 2) {
		console_printf("phy_cmd_read <idx>\n");
		return;
	}
	
	// 値設定
	idx = atoi(argv[1]);
	
	// レジスタのベースアドレスを取得
	p_reg = ch_info_tbl.p_reg;
	
	// 送信
	ercd = phy_read(p_reg, idx, &data);
	console_printf("ercd:%d, data:%x\n", ercd, data);
}

// コマンド設定関数
void eth_set_cmd(void)
{
	COMMAND_INFO cmd;
	
	// コマンドの設定
	cmd.input = "mmd_cmd_read";
	cmd.func = mmd_cmd_read;
	console_set_command(&cmd);
	cmd.input = "mmd_cmd_write";
	cmd.func = mmd_cmd_write;
	console_set_command(&cmd);
	// コマンドの設定
	cmd.input = "phy_cmd_read";
	cmd.func = phy_cmd_read;
	console_set_command(&cmd);
}
