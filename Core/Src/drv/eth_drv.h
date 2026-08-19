/*
 * eth_drv.h
 *
 *  Created on: May 6, 2026
 *      Author: hcuym
 */

#ifndef SRC_DRV_ETH_DRV_H_
#define SRC_DRV_ETH_DRV_H_

#include "eth.h"

#define ETHER_ADDR_LEN		(ETH_MACADDR_SIZE)
#define ETHER_ADDR_STR_LEN	(18) 					/* "xx:xx:xx:xx:xx:xx\0" */
#define ETHER_TYPE_IP		(0x0800)
#define ETHER_TYPE_ARP		(0x0806)

#define ETHER_HDR_SIZE 14
#define ETHER_FRAME_SIZE_MIN   60 /* without FCS */
#define ETHER_FRAME_SIZE_MAX 1514 /* without FCS */
#define ETHER_PAYLOAD_SIZE_MIN (ETHER_FRAME_SIZE_MIN - ETHER_HDR_SIZE)
#define ETHER_PAYLOAD_SIZE_MAX (ETHER_FRAME_SIZE_MAX - ETHER_HDR_SIZE)

typedef enum {
	ETH_DRV_CH_1 = 0,
	ETH_DRV_CH_MAX,
} ETH_DRV_CH;

extern const uint8_t ETHER_ADDR_BROADCAST[ETHER_ADDR_LEN];

extern osStatus eth_drv_init(void);
extern osStatus eth_drv_open(ETH_DRV_CH ch, char *mac_addr, void *cb_vp);
extern osStatus eth_drv_send(ETH_DRV_CH ch, uint8_t *p_data, uint16_t size, uint32_t timeout);

extern char * ether_drv_addr_ntop(const uint8_t *n, char *p, size_t size);
extern int ether_drv_addr_pton(const char *p, uint8_t *n);

#endif /* SRC_DRV_ETH_DRV_H_ */
