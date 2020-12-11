#ifndef AES_H
#define AES_H

/**
 * 参数 p: 明文的字符串数组，长度必须为16。
 * 参数 key: 密钥的字符串数组，长度必须为16。
 */
// void aes(char *p, char *key);

/**
 * 参数 c: 密文的字符串数组，长度必须为16的倍数。
 * 参数 len: 密文的字符串数组的长度，必须为16的倍数。
 * 参数 key: 密钥的字符串数组，长度必须为16。
 * 参数 random: 起始CTR存放位置，确保有至少16字节的空间。
 * 参数 mac：校验值存放位置，确保有至少16字节的空间。
 */
int encode(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], char (&random)[16], char (&mac)[16]);

/**
 * 参数 c: 密文的字符串数组，长度必须为16的倍数。
 * 参数 len: 密文的字符串数组的长度，必须为16的倍数。
 * 参数 key: 密钥的字符串数组，长度必须为16。
 * 参数 random: 起始CTR值，长度为16。
 * 参数 mac：校验值，长度为16。
 */
int decode(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], const char (&random)[16], const char (&mac)[16]);

/**
 * dst: 密文存放位置
 * dst_size: 密文存放位置的长度，如果实际密文长度超过该长度，返回-1
 * src: 明文起始位置
 * src_size: 明文长度
 * key: 密钥起始位置，密钥长度固定为16 byte
 * 
 * return 0 if success; otherwise return -1
 */
int encrypt(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], char (&random)[16], char (&mac)[16]);

int decrypt(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], const char (&random)[16], const char (&mac)[16]);

#endif

