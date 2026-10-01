/*
 * console.c
 *
 *  Created on: 2024/10/09
 *      Author: mito
 */
#include <stdarg.h>
#include <string.h>
#include "cmsis_os.h"
#include "console.h"
#include "usart_drv.h"

#define CONOLE_BUF_SIZE		(64)		// コマンドラインバッファサイズ
#define STACK_SIZE			(512)		// スタックサイズ
#define CONOLE_CMD_NUM		(16)		// 設定できるコマンドの数
#define CONSOLE_ARG_MAX		(10)		// 引数の個数の最大値
#define CONSOLE_SEND_MAX	(128)		// コンソール出力する最大の文字数

#define CONSOLE_SEND_TASK	(0)
#define CONSOLE_RECV_TASK	(1)
#define CONSOLE_TASK_MAX	(2)

// 表示型
typedef struct {
	char output[CONSOLE_SEND_MAX];
} OUTPUT;

// 制御ブロック
typedef struct {
	osThreadId 		ConsoleSendTaskHandle;		// コンソール送信タスク
	osThreadId 		ConsoleRecvTaskHandle;		// コンソール受信タスク
	osMailQId		ConsoleSendMailHandle;		// コンソール送信バッファ
	char			buf[CONOLE_BUF_SIZE];		// コマンドラインバッファ
	uint8_t			buf_idx;					// コマンドラインバッファインデックス
	COMMAND_INFO	cmd_info[CONOLE_CMD_NUM];	// コマンド関数
	uint8_t			cmd_idx;					// コマンド関数インデックス
} CONSOLE_CB;
static CONSOLE_CB console_cb;
#define get_myself() (&console_cb)

// 特定の文字位置を取得
uint8_t find_str(char str, char *data)
{
	char *start_addr, *end_addr;
	uint8_t pos = 0;
	
	// 開始アドレスを取得
	start_addr = data;
	
	// 特定の文字の位置を取得
	end_addr = strchr(data, str);
	
	if (end_addr != NULL) {
		pos = (uint8_t)(end_addr - start_addr);
	}
	
	return pos;
}

/**
**---------------------------------------------------------------------------
**  Abstract: Convert integer to ascii
**  Returns:  void
**---------------------------------------------------------------------------
*/
void ts_itoa(char **buf, unsigned int d, int base)
{
	int div = 1;
	while (d/div >= base)
		div *= base;

	while (div != 0)
	{
		int num = d/div;
		d = d%div;
		div /= base;
		if (num > 9)
			*((*buf)++) = (num-10) + 'A';
		else
			*((*buf)++) = num + '0';
	}
}

/**
**---------------------------------------------------------------------------
**  Abstract: Calculate maximum length of the resulting string from the
**            format string and va_list va
**  Returns:  Maximum length
**---------------------------------------------------------------------------
*/
static int ts_formatlength(const char *fmt, va_list va)
{
	int length = 0;
	while (*fmt)
	{
		if (*fmt == '%')
		{
			++fmt;
			switch (*fmt)
			{
			  case 'c':
		  		  va_arg(va, int);
				  ++length;
				  break;
			  case 'd':
			  case 'i':
			  case 'u':
				  /* 32 bits integer is max 11 characters with minus sign */
				  length += 11;
				  va_arg(va, int);
				  break;
			  case 's':
			  	  {
			  		  char * str = va_arg(va, char *);
			  		  while (*str++)
			  			  ++length;
			  	  }
				  break;
			  case 'x':
			  case 'X':
				  /* 32 bits integer as hex is max 8 characters */
				  length += 8;
				  va_arg(va, unsigned int);
				  break;
			  default:
				  ++length;
				  break;
			}
		}
		else
		{
			++length;
		}
		++fmt;
	}
	return length;
}

/**
**---------------------------------------------------------------------------
**  Abstract: Writes arguments va to buffer buf according to format fmt
**  Returns:  Length of string
**---------------------------------------------------------------------------
*/
int ts_formatstring(char *buf, const char *fmt, va_list va)
{
	char *start_buf = buf;
	while(*fmt)
	{
		/* Character needs formating? */
		if (*fmt == '%')
		{
			switch (*(++fmt))
			{
			  case 'c':
				*buf++ = va_arg(va, int);
				break;
			  case 'd':
			  case 'i':
				{
					signed int val = va_arg(va, signed int);
					if (val < 0)
					{
						val *= -1;
						*buf++ = '-';
					}
					ts_itoa(&buf, val, 10);
				}
				break;
			  case 's':
				{
					char * arg = va_arg(va, char *);
					while (*arg)
					{
						*buf++ = *arg++;
					}
				}
				break;
			  case 'u':
					ts_itoa(&buf, va_arg(va, unsigned int), 10);
				break;
			  case 'x':
			  case 'X':
					ts_itoa(&buf, va_arg(va, int), 16);
				break;
			  case '%':
				  *buf++ = '%';
				  break;
			}
			fmt++;
		}
		/* Else just copy */
		else
		{
			*buf++ = *fmt++;
		}
	}
	*buf = 0;

	return (int)(buf - start_buf);
}

// コンソールからの入力を受信する関数
static uint8_t console_recv(void)
{
	uint8_t data;
	int32_t size;
	
	while(1) {
		// 受信できるで待つ
		size = usart_drv_recv(USART_DRV_DEV_CONSOLE, &data, 1, 1000);
		// 期待したサイズ読めた？
		if (size == 1) {
			break;
		}
	}
	
	return data;
}

// コンソールからの入力を受信する関数
static void console_analysis(uint8_t data)
{
	CONSOLE_CB *this = get_myself();
	COMMAND_INFO *cmd_info;
	uint8_t i, j;
	uint8_t argc = 0;
	char *argv[CONSOLE_ARG_MAX];
	uint8_t base_pos = 0;
	uint8_t sp_pos = 0;
	
	switch (data) {
		case '\t':	// tab
			// コマンドの一覧を表示
			console_printf("\n");
			for (i = 0; i < this->cmd_idx; i++) {
				cmd_info = &(this->cmd_info[i]);
				console_printf(cmd_info->input);
				console_printf("\n");
			}
			console_printf("\n");
			break;
		case '\b':	// back space
			break;
		case '\n':	// Enter
			// NULL文字を設定
			this->buf[this->buf_idx++] = '\0';
			// コマンドに設定されている？
			for (i = 0; i < this->cmd_idx; i++) {
				cmd_info = &(this->cmd_info[i]);
				// コマンド名が一致した
				if (memcmp(this->buf, cmd_info->input, strlen(cmd_info->input)) == 0) {
					// 引数解析 (*) 先頭に空白は絶対に入れないこと！！
					for (j = 0; j < CONSOLE_ARG_MAX; j++) {
						// 空白を検索
						sp_pos = find_str(' ', &(this->buf[base_pos]));
						// 引数設定
						argv[argc++] = &(this->buf[base_pos]);
						// 空白がなかった
						if (sp_pos == 0) {
							break;
						}
						// 空白をNULL文字に設定
						this->buf[base_pos+sp_pos] = '\0';
						// 検索開始位置を更新 
						base_pos += sp_pos + 1;
					}
					// コマンド実行
					cmd_info->func(argc, argv);
					break;
				}
			}
			// その他あればここで処理する
			// コマンドラインバッファインデックスをクリア
			this->buf_idx = 0;
			break;
		default:
			// データをバッファを格納
			this->buf[this->buf_idx++] = data;
			break;
	}
	
	return;
}

// コンソール送信タスク
void StartConsoleSend(void const * argument)
{
	CONSOLE_CB *this =  get_myself();
	osEvent evt;
	int32_t ercd;
	int8_t print_buf[CONSOLE_SEND_MAX];
	uint32_t size;
	
	while (1) {
		// 送信データ待ち
		evt = osMailGet(this->ConsoleSendMailHandle, 10);
		// イベントがないなら次の送信データを待つ
		if (evt.status == osEventMail) {
			// 初期化
			memset(print_buf, 0x00, sizeof(print_buf));
			// サイズ取得
			size = strlen(evt.value.p);
			// 早く開放したいからローカル変数にコピー
			memcpy(print_buf, evt.value.p, size + 1);	// + 1は'\0'
			// 解放
			osMailFree(this->ConsoleSendMailHandle, evt.value.p);
			// コンソール出力
			ercd = usart_drv_send(USART_DRV_DEV_CONSOLE, (uint8_t*)print_buf, size, 10);
			if (ercd < 0) {
				// エラー処理
			}
		}
	}
}

// コンソール受信タスク
void StartConsoleRecv(void const * argument)
{
	CONSOLE_CB *this =  get_myself();
	uint8_t data[2];
	
	while (1) {
		// "command>"を出力
		if (this->buf_idx == 0) {
			console_printf("command>");
		}
		// コンソールからの入力を受信する
		data[0] = console_recv();
		data[1] = '\0';
		// 改行コード変換(\r→\n)
		if (data[0] == '\r') data[0] = '\n';
		// エコーバック
		console_printf("%c", data[0]);
		// 受信データ解析
		console_analysis(data[0]);
	}
}

// 初期化
osStatus console_init(void)
{
	CONSOLE_CB *this =  get_myself();
	uint32_t ercd;
	
	// 初期化
	memset(this, 0x00, sizeof(CONSOLE_CB));
	
	// オープン
	if ((ercd = usart_drv_open(USART_DRV_DEV_CONSOLE)) != osOK) {
		goto EXIT;
	}
	
	// メールキュー作成
	osMailQDef(ConsoleSendBuf, 64, OUTPUT);
	this->ConsoleSendMailHandle = osMailCreate(osMailQ(ConsoleSendBuf), NULL);
	
	osThreadDef(ConsoleSend, StartConsoleSend, osPriorityNormal, 0, 512);
	this->ConsoleSendTaskHandle = osThreadCreate(osThread(ConsoleSend), NULL);
	
	osThreadDef(ConsoleRecv, StartConsoleRecv, osPriorityLow, 0, 512);
	this->ConsoleRecvTaskHandle = osThreadCreate(osThread(ConsoleRecv), NULL);
	
EXIT:
	return ercd;
}

void console_printf(const char *fmt, ...)
{
	CONSOLE_CB *this = get_myself();
	int32_t length = 0;
	va_list va;
	char *output = NULL;
	
	va_start(va, fmt);
	length = ts_formatlength(fmt, va);
	va_end(va);
	
	// 最大数を超えている場合は終了
	if (length > CONSOLE_SEND_MAX) {
		return;
	}
	
	// メモリ確保
	output = osMailAlloc(this->ConsoleSendMailHandle, osWaitForever);
	if (output == NULL) {
		return;
	}
	
	// 初期化
	memset(output, 0x00, CONSOLE_SEND_MAX);
	
	va_start(va, fmt);
	length = ts_formatstring(output, fmt, va);
	// コンソール送信タスクへ送信
	osMailPut(this->ConsoleSendMailHandle, output);
	va_end(va);
	
}

// コマンドを設定する関数
osStatus console_set_command(COMMAND_INFO *cmd_info)
{
	CONSOLE_CB *this;
	
	// コマンド関数がNULLの場合エラーを返して終了
	if (cmd_info == NULL) {
		return -1;
	}
	
	// 制御ブロックの取得
	this = get_myself();
	
	// もうコマンド関数を登録できない場合はエラーを返して終了
	if (this->cmd_idx >= CONOLE_CMD_NUM) {
		return -1;
	}
	
	// 登録
	this->cmd_info[this->cmd_idx].input = cmd_info->input;
	this->cmd_info[this->cmd_idx].func = cmd_info->func;
	this->cmd_idx++;
	
	return 0;
}

