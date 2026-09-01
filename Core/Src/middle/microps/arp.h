/*
 * arp.h
 *
 *  Created on: Aug 20, 2026
 *      Author: hcuym
 */

#ifndef SRC_MIDDLE_MICROPS_ARP_H_
#define SRC_MIDDLE_MICROPS_ARP_H_

#define ARP_RESOLVE_ERROR      -1
#define ARP_RESOLVE_INCOMPLETE  0
#define ARP_RESOLVE_FOUND       1

extern osStatus arp_init(void);
extern int arp_resolve(struct net_iface *iface, ip_addr_t pa, uint8_t *ha);

#endif /* SRC_MIDDLE_MICROPS_ARP_H_ */
