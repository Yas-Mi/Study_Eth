/*
 * udp.h
 *
 *  Created on: Sep 16, 2026
 *      Author: hcuym
 */

#ifndef SRC_MIDDLE_MICROPS_UDP_H_
#define SRC_MIDDLE_MICROPS_UDP_H_

extern osStatus udp_init(void);
extern int32_t udp_cmd_open(void);
extern osStatus udp_cmd_close(int32_t desc);
extern osStatus udp_cmd_bind(int32_t desc, ip_endp_t local);

#endif /* SRC_MIDDLE_MICROPS_UDP_H_ */
