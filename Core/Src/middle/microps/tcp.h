/*
 * tcp.h
 *
 *  Created on: Nov 14, 2026
 *      Author: hcuym
 */
 
extern osStatus tcp_init(void);
extern int32_t tcp_cmd_open(ip_endp_t local, ip_endp_t remote, int32_t active);
extern osStatus tcp_cmd_close(int32_t desc);
