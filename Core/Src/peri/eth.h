/*
 * eth.h
 *
 *  Created on: 2025/9/8
 *      Author: user
 */

#ifndef SRC_PERI_ETH_H_
#define SRC_PERI_ETH_H_

// 定義
#define TX_DISCRIPTOR_NUM		(8)		// 送信ディスクリプタ数
#define RX_DISCRIPTOR_NUM		(8)		// 受信ディスクリプタ数
#define ETH_MACADDR_SIZE		(6)		// マックアドレス長

// 送受信コールバック
typedef void (*ETH_TX_CALLBACK)(void *vp, uint32_t ret);
typedef void (*ETH_RX_CALLBACK)(void *vp, uint32_t ret);
typedef void (*ETH_ERR_CALLBACK)(void *vp, uint32_t ret);

typedef enum {
	ETH_CH_1 = 0,
	ETH_CH_MAX,
} ETH_CH;

typedef enum {
	COM_MODE_HALF_DUPLEX = 0,	// 半二重
	COM_MODE_FULL_DUPLEX,		// 全二重
	COM_MODE_MAX
} COM_MODE;

typedef struct {
	COM_MODE 			mode;		// 通信方式
	ETH_TX_CALLBACK		tx_cb;		// 送信コールバック
	ETH_RX_CALLBACK		rx_cb;		// 受信コールバック
	ETH_ERR_CALLBACK 	err_cb;		// エラーコールバック
	void*				cb_vp;		// コールバックパラメータ
	uint8_t				mac_addr[ETH_MACADDR_SIZE];	// マックアドレス
} ETH_OPEN;

extern void eth_init(void);
extern osStatus eth_open(ETH_CH ch, ETH_OPEN *p_par);
extern osStatus eth_send(ETH_CH ch, uint8_t *p_data, uint32_t size);
extern osStatus eth_recv(ETH_CH ch, uint8_t *p_data, uint32_t *p_size);
extern osStatus eth_go_down(void);

void eth_set_cmd(void);

#endif /* SRC_PERI_ETH_H_ */
 