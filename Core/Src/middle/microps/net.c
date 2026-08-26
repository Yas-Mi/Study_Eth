#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cmsis_os.h"
#include "util.h"
#include "ip.h"
#include "icmp.h"
#include "arp.h"

#include "net.h"

#define NET_DEV_NUM		(4)
#define NET_PROTO_NUM	(4)

// プロトコル構造体
struct net_protocol {
	struct net_protocol *next;		// 連結リストの次の要素をさすポインタ
	uint16_t type;					// プロトコルの種別を表す値
	net_prtocol_handler_t handler;	// プロトコルのパケットを処理する入力ハンドラの関数ポインタ
};

// 制御ブロック
typedef struct {
	osPoolId   net_dev_id;					// ネットワークデバイス用のメモリプールID
	osPoolId   net_proto_id;				// ネットワークプロトコル用のメモリプールID
	struct net_device *devices;				// ネットワークデバイス管理リスト
	uint32_t index;							// ネットワークデバイスのインデックス番号
	struct net_protocol *protocols;	//プロトコルの登録リスト
}NET_CB;
static NET_CB net_cb;
#define get_myself() (&net_cb)

// ネットワークデバイスのオブジェクト割り当て
// 説明：動的に確保したネットワークデバイス構造体のメモリを返す
struct net_device *net_device_alloc(void)
{
	NET_CB *this = get_myself();
	struct net_device *dev;
	
	// ネットワークデバイス用のメモリを確保
	dev = osPoolCAlloc(this->net_dev_id);
	if (dev == NULL) {
		errorf("osPoolCAlloc() failure");
		return NULL;
	}
	return dev;
}

// ネットワークプロトコルの取得
struct net_protocol *net_protocol_alloc(void)
{
	NET_CB *this = get_myself();
	struct net_protocol *proto;
	
	// ネットワークプロトコルのメモリを確保
	proto = osPoolCAlloc(this->net_proto_id);
	if (proto == NULL) {
		errorf("osPoolCAlloc() failure");
		return NULL;
	}
	return proto;
}

// ネットワークデバイスの登録
// 説明：ネットワークデバイスをプロトコルスタックのメインモジュール内で管理しているネットワークデバイスの「登録リスト」へ追加
osStatus net_device_register(struct net_device *dev) 
{
	NET_CB *this = get_myself();
	static uint32_t index = 0;
	
	// 追加
	dev->index = this->index++;
	sprintf(dev->name, "net%d", dev->index);
	dev->next = this->devices;
	this->devices = dev;
	
	infof("registered, dev=%s", dev->name);
	
	return osOK;
}

// ネットワークデバイスの起動
// 説明：引数で渡されたネットワークデバイスを起動させ、稼働フラグをセット
static osStatus net_device_open(struct net_device *dev)
{
	infof("dev=%s", dev->name);
	if (NET_DEVICE_IS_UP(dev)) {
		errorf("already opend, dev=%s", dev->name);
		return osErrorResource;
	}
	// デバイス固有のオープン関数を実行
	if (dev->ops->open != NULL) {
		if (dev->ops->open(dev) != osOK) {
			errorf("failure, dev=%s", dev->name);
			return osErrorResource;
		}
	}
	SET_DEVICE_FLAG(dev, NET_DEVICE_FLAG_UP);
	
	return osOK;
}

// ネットワークデバイスの停止
// 説明：引数で渡されたネットワークデバイスを起動させ、稼働フラグをセット
static osStatus net_device_close(struct net_device *dev)
{
	infof("dev=%s", dev->name);
	if (NET_DEVICE_IS_UP(dev) != 0) {
		errorf("not opend, dev=%s", dev->name);
		return osErrorResource;
	}
	// デバイス固有のクローズ関数を実行
	if (dev->ops->close != NULL) {
		if (dev->ops->close(dev) != osOK) {
			errorf("failure, dev=%s", dev->name);
			return osErrorResource;
		}
	}
	CLR_DEVICE_FLAG(dev, NET_DEVICE_FLAG_UP);
	
	return osOK;
}

osStatus net_init(void)
{
	NET_CB *this = get_myself();
	
	infof("initialize...");
	
	// 制御ブロック初期化
	memset(this, 0, sizeof(NET_CB));
	// ネットワークデバイス用のメモリプール確保
	osPoolDef(MemPool_1, NET_DEV_NUM, struct net_device);
	this->net_dev_id = osPoolCreate (osPool (MemPool_1));
	if (this->net_dev_id == NULL) {
		return osErrorOS;
	}
	// ネットワークプロトコル用のメモリプール確保
	osPoolDef(MemPool_2, NET_PROTO_NUM, struct net_protocol);
	this->net_proto_id = osPoolCreate (osPool (MemPool_2));
	if (this->net_proto_id== NULL) {
		return osErrorOS;
	}
	// arp初期化
	if (arp_init() != osOK) {
		errorf("ip_init() failure");
		return osErrorResource;	
	}
	// ip初期化
	if (ip_init() != osOK) {
		errorf("ip_init() failure");
		return osErrorResource;	
	}
	// icmp初期化
	if (icmp_init() != osOK) {
		errorf("icmp_init() failure");
		return osErrorResource;	
	}
	infof("success...");
	
	return osOK;
}

osStatus net_run(void)
{
	NET_CB *this = get_myself();
	struct net_device *dev;
	
	infof("startup...");
	// オープン
	for (dev = this->devices; dev; dev = dev->next) {
		net_device_open(dev);
	}
	infof("success...");
	
	return osOK;
}

osStatus net_shutdown(void)
{
	NET_CB *this = get_myself();
	struct net_device *dev;
	
	infof("shutting down...");
	// クローズ
	for (dev = this->devices; dev; dev = dev->next) {
		net_device_close(dev);
	}	
	infof("success...");
	
	return osOK;
}

// ネットワークデバイスへの出力
osStatus net_device_output(struct net_device *dev, uint16_t type, const uint8_t *data, size_t len, const void *dst)
{
	debugf("dev=%s, type=0x%x, len=%zu", dev->name, type, len);
	
	// オープンしてなかったらダメ
	if (!NET_DEVICE_IS_UP(dev)) {
		errorf("not opend, dev=%s", dev->name);
		return osErrorResource;
	}
	// mtu以上だったらダメ
	if (dev->mtu < len) {
		errorf("too long, dev=%s, mtu=%u, len=%zu", dev->name, dev->mtu, len);
		return osErrorResource;
	}
	// output関数ちゃんとある？
	if (dev->ops->output == NULL) {
		errorf("output callback function is not set, dev=%s", dev->name);
		return osErrorResource;
	}
	// output関数実行
	if (dev->ops->output(dev, type, data, len, dst) != osOK) {
		errorf("failure, dev=%s, len=%d", dev->name, len);
		return osErrorResource;
	}
	
	return osOK;
}

// ネットワークデバイスからの入力
osStatus net_input(uint16_t type, const uint8_t *data, size_t len, struct net_device *dev)
{
	NET_CB *this = get_myself();
	struct net_protocol *proto;
	
	debugf("dev=%s, type=%x, len=%d", dev->name, type, len);
	//HEXDUMP(data, len);
	
	// タイプによって通知する上位層を決める
	for (proto = this->protocols; proto; proto = proto->next) {
		if (type == proto->type) {
			proto->handler(data, len, dev);
			return osOK;
		}
	}
	// unsuported protocol
	return osOK;
}

// プロトコルの登録
osStatus net_protocol_register(uint16_t type, net_prtocol_handler_t handler)
{
	NET_CB *this = get_myself();
	struct net_protocol *proto;
	
	// 既に登録されているかをチェック
	for (proto = this->protocols; proto; proto = proto->next) {
		if (type == proto->type) {
			errorf("already registerd, type=%s", proto->type);
			return osErrorResource;	
		}
	}
	// メモリ確保
	proto = net_protocol_alloc();
	if (proto == NULL) {
		errorf("net_protocol_alloc() failure");
		return osErrorResource;
	}
	proto->type = type;
	proto->handler = handler;
	proto->next = this->protocols;
	this->protocols = proto;
	infof("shutting down...");
	
	return osOK;
}

// 引数で指定したネットワークデバイスにインタフェースを紐づける
osStatus net_device_add_iface(struct net_device *dev, struct net_iface *iface)
{
	struct net_iface *entry;
	
	// 重複登録のチェック
	for (entry = dev->ifaces; entry; entry = entry->next) {
		if (entry->family == iface->family) {
			errorf("already exist, dev=%s、family=%d", dev->name, entry->family);
			return osErrorResource;
		}
	}
	iface->next = dev->ifaces;
	iface->dev = dev;
	dev->ifaces = iface;
	
	infof("success, dev=%s", dev->name);
	
	return osOK;
	
}

// ネットワークデバイスに紐づけられているインタフェースの取得
struct net_iface * net_device_get_iface(struct net_device *dev, int family)
{
	struct net_iface *entry;
	
	for (entry = dev->ifaces; entry; entry = entry->next) {
		if (entry->family == family) {
			break;
		}
	}
	return entry;
}

