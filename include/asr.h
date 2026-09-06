/*
 * asr.h - 语音识别模块(百度短语音识别 API)
 * 通过 HTTPS(WinHTTP) 调用百度智能云语音识别服务, 将 16kHz 单声道 PCM 音频转为文字。
 * 密钥从 data/asr_config.json 读取。
 */
#ifndef LS_ASR_H
#define LS_ASR_H

#include "common.h"

/* 语音识别配置 */
typedef struct {
    char api_key[128];    /* 百度智能云应用的 API Key */
    char secret_key[128]; /* 百度智能云应用的 Secret Key */
    char cuid[64];        /* 设备标识 */
} ls_asr_config;

/* 从 JSON 文件加载配置, 返回 0 成功 */
int ls_asr_load_config(const char* path, ls_asr_config* cfg);

/* 百度短语音识别: pcm 为 16kHz 单声道 PCM 数据, 长度 pcm_len 字节。
 * 返回识别文本(堆上字符串, 调用方 free), 失败返回 NULL。
 * 未配置密钥时返回 NULL 并在 *err 中给出原因。 */
char* ls_asr_recognize(const char* data_dir, const unsigned char* pcm, int pcm_len, char* err, size_t err_n);

/* 同 ls_asr_recognize, 但直接接收 base64 编码的 PCM 文本(免去调用方解码)。
 * 返回识别文本(堆上字符串, 调用方 free), 失败返回 NULL。 */
char* ls_asr_recognize_base64(const char* data_dir, const char* b64, char* err, size_t err_n);

#endif /* LS_ASR_H */
