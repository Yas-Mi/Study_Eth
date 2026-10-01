/*
 * eth.h
 *
 *  Created on: 2026/7/1
 *      Author: ronald
 */

#ifndef NET_H_
#define NET_H_

#ifndef IFNAMSIZ
#define IFNAMSIZ	(16)
#endif

#define NET_DEVICE_TYPE_DUMMY		(0x0000)
#define NET_DEVICE_TYPE_LOOPBACK	(0x0001)
#define NET_DEVICE_TYPE_ETHRNET		(0x0002)

#define NET_DEVICE_FLAG_UP			(0x0001)
#define NET_DEVICE_FLAG_LOOPBACK	(0x0010)
#define NET_DEVICE_FLAG_BROADCAST	(0x0020)
#define NET_DEVICE_FLAG_P2P			(0x0040)
#define NET_DEVICE_FLAG_NEED_ARP	(0x0100)

#define NET_DEVICE_ADDR_LEN			(16)

// プロトコル種別
#define NET_PROTOCOL_TYPE_IP		(0x0800)
#define NET_PROTOCOL_TYPE_ARP		(0x0806)
#define NET_PROTOCOL_TYPE_IPV6		(0x86dd)

#define NET_DEVICE_IS_UP(x)			((x)->flags & NET_DEVICE_FLAG_UP)
#define SET_DEVICE_FLAG(x,flag)	((x)->flags |= flag)
#define CLR_DEVICE_FLAG(x,flag)	((x)->flags &= ~flag)

#define NET_IFACE_FAMILY_IP			(1)

// インターフェース構造体
struct net_device;
struct net_iface {
	struct net_iface *next;		// 連結リストの次の要素をさすポインタ
	struct net_device *dev;		// インタフェースが紐づけられているネットワークデバイス
	int32_t family;				// どのプロトコルスイートのためのインタフェースなのかを示す
	// depends on implenmentation of protocols
};

struct net_device {
	struct net_device *next;				// 連結リストの次の要素をさすポインタ
	struct net_iface *ifaces;				// ネットワークデバイスに紐づけられているインタフェースのリスト
	uint32_t index;							// ネットワークデバイスを一意に識別するためのインデックス番号
	char name[IFNAMSIZ];					// ネットワークデバイス名
	uint16_t type;							// ネットワークデバイスの種別を表す値
	uint16_t mtu;							// MTU
	uint16_t flags;							// ネットワークデバイスの特性と状態を表すフラグ値の集合
	uint16_t hlen;							// データリンクのヘッダ長
	uint16_t alen;							// データリンクのアドレス長
	uint8_t addr[NET_DEVICE_ADDR_LEN];		// ネットワークデバイスのアドレス
	uint8_t bloadcast[NET_DEVICE_ADDR_LEN];	// データリンクのブロードキャストアドレス
	struct net_device_ops *ops;				// デバイス固有の処理を行う関数のアドレスを格納する構造体のポインタ
	void *priv;								// 任意のデータをさすポインタ
};

struct net_device_ops {
	osStatus (*open)(struct net_device *dev);
	osStatus (*close)(struct net_device *dev);
	osStatus (*output)(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst);
};

struct ip_iface {
	struct net_iface	ifaces;		// ネットワークデバイスへの紐づけのために使用する汎用的なインタフェース構造体
	struct ip_iface		*next;		// 連結リストの次の要素をさすポインタ
	ip_addr_t			unicast;	// IPインタフェースのユニキャストIPアドレス
	ip_addr_t			netmask;	// IPインタフェースのサブネットマスク
	ip_addr_t			broadcast;	// IPインタフェースのブロードキャストIPアドレス
};
#define NET_IFACE(x)	((struct net_iface *)(x))

typedef void (*net_prtocol_handler_t)(const uint8_t *data, size_t len, struct net_device *dev);

extern osStatus net_init(void);
extern osStatus net_run(void);
extern osStatus net_shutdown(void);
extern osStatus net_device_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst);
extern osStatus net_input(uint16_t type, const uint8_t *data, size_t len, struct net_device *dev);
extern struct net_device *net_device_alloc(void);
extern osStatus net_device_register(struct net_device *dev);
extern osStatus net_protocol_register(uint16_t type, net_prtocol_handler_t handler);
extern osStatus net_device_add_iface(struct net_device *dev, struct net_iface *iface);
extern struct net_iface * net_device_get_iface(struct net_device *dev, int family);

#endif /* NET_H_ */
 

