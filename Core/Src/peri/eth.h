/*
 * eth.h
 *
 *  Created on: 2025/9/8
 *      Author: user
 */

#ifndef SRC_PERI_ETH_H_
#define SRC_PERI_ETH_H_

typedef enum {
	COM_MODE_HALF_DUPLEX = 0,	// 半二重
	COM_MODE_FULL_DUPLEX,		// 全二重
	COM_MODE_MAX
} COM_MODE;

typedef struct {
	COM_MODE mode;	// 通信方式
} ETH_OPEN;

extern void eth_init(void);
extern osStatus eth_open(ETH_OPEN *p_par);
extern osStatus eth_send(uint8_t *p_data, uint32_t size);

void eth_set_cmd(void);

#endif /* SRC_PERI_ETH_H_ */
 