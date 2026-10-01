#include "console.h"

#define logf(fmt, ...)  console_printf(fmt, ##__VA_ARGS__)

#define errorf(fmt, ...) logf("[E]:"fmt" (%s, %d, %s)\n", ##__VA_ARGS__, __FILE__, __LINE__, __func__)	// エラー用
#define warnf(fmt, ...) logf("[W]:"fmt" (%s, %d, %s)\n", ##__VA_ARGS__, __FILE__, __LINE__, __func__)	// 警告用
#define infof(fmt, ...) logf("[I]:"fmt"\n", ##__VA_ARGS__)													// 情報用
#define debugf(fmt, ...) logf("[D]:"fmt" (%s, %d, %s)\n", ##__VA_ARGS__, __FILE__, __LINE__, __func__)	// デバッグ用

#define countof(x) ((sizeof(x) / sizeof(*x)))
#define tailof(x) (x + countof(x))
#define indexof(x, y) (((uintptr_t)y - (uintptr_t)x) / sizeof(*y))

// Hexdump
#define HEXDUMP(buf, len)
#if 0
#define HEXDUMP(buf, len) do {                                      \
    const unsigned char *p = (const unsigned char *)(buf);          \
    size_t _i, _j;                                                   \
    for (_i = 0; _i < (len); _i += 16) {                             \
        console_printf("%08zx  ", _i);                               \
        for (_j = 0; _j < 16; _j++) {                                \
            if (_i + _j < (len))                                     \
                console_printf("%02x ", p[_i + _j]);                 \
            else                                                     \
                console_printf("   ");                               \
        }                                                            \
        console_printf(" ");                                         \
        for (_j = 0; _j < 16; _j++) {                                \
            if (_i + _j < (len)) {                                   \
                unsigned char c = p[_i + _j];                        \
                console_printf("%c", isprint(c) ? c : '.');          \
            }                                                        \
        }                                                            \
        console_printf("\n");                                        \
    }                                                                \
} while (0)
#endif

struct queue_entry {
	struct queue_entry	*next;
	void *data;
	// data bytes exists after this sturucture
};

struct queue {
	struct queue_entry		*head;
	struct queue_entry		*tail;
	size_t num;
};

typedef void (*queue_func_t)(void *arg, struct queue_entry *entry);

extern uint16_t cksum16(uint16_t *addr, uint16_t count, uint32_t init);
extern uint16_t hton16(uint16_t h);
extern uint16_t ntoh16(uint16_t n);
extern uint32_t hton32(uint32_t h);
extern uint32_t ntoh32(uint32_t h);

extern osStatus util_init(void);
extern uint16_t rondom16(void);

extern void queue_init(struct queue *queue);
extern struct queue_entry * queue_push(struct queue *queue, void *data);
extern void * queue_pop(struct queue *queue);
extern void * queue_peek(struct queue *queue);
extern void queue_foreach(struct queue *queue, queue_func_t func, void *arg);

