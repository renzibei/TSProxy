#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aes.h"
#include "loghelper.h"
#include "constants.h"

/**
 * S盒
 */
static const int S[16][16] = {
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
	0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
	0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
	0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
	0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
	0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
	0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
	0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
	0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
	0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
	0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
	0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
	0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
	0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
	0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
	0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 };

/**
 * 根据索引，从盒中获得元素
 */
static int getNumFromBox(const int (&box)[16][16], int index) {
	int row = (index & 0x000000f0) >> 4;
	int col = index & 0x0000000f;
	return box[row][col];
}

/**
 * 把一个字符转变成整型
 */
static int charToInt(char c) {
	int result = (int) c;
	return result & 0x000000ff;
}

/**
 * 把16个字符转变成4X4的数组，
 * 该矩阵中字节的排列顺序为从上到下，
 * 从左到右依次排列。
 */
static void strToArray(char *str, int (&array)[4][4]) {
	int i, j;
	for(i = 0; i < 4; i++){
		for(j = 0; j < 4; j++) {
			array[j][i] = charToInt(str[4*i+j]);
		}
	}
}

/**
 * 把一个4字节的数的第一、二、三、四个字节取出，
 * 入进一个4个元素的整型数组里面。
 */
static void intToArray(int num, int (&array)[4]) {
	int one, two, three;
	one = num >> 24;
	array[0] = one & 0x000000ff;
	two = num >> 16;
	array[1] = two & 0x000000ff;
	three = num >> 8;
	array[2] = three & 0x000000ff;
	array[3] = num & 0x000000ff;
}

/**
 * 把数组中的第一、二、三和四元素分别作为
 * 4字节整型的第一、二、三和四字节，合并成一个4字节整型
 */
static int arrayToInt(int (&array)[4]) {
	int one = array[0] << 24;
	int two = array[1] << 16;
	int three = array[2] << 8;
	int four = array[3];
	return one | two | three | four;
}

/**
 * 常量轮值表
 */
static const uint32_t Rcon[10] = {
    0x01000000, 0x02000000,
	0x04000000, 0x08000000,
	0x10000000, 0x20000000,
	0x40000000, 0x80000000,
	0x1b000000, 0x36000000 };
/**
 * 密钥扩展中的T函数
 */
static int T(int num, int round) {
	int numArray[4];
	int i;
	int result;
	// 循环左移 1 位填入
	numArray[3] = (num >> 24) & 0x000000ff;
	numArray[0] = (num >> 16) & 0x000000ff;
	numArray[1] = (num >> 8) & 0x000000ff;
	numArray[2] = num & 0x000000ff;

	//字节代换
	for(i = 0; i < 4; i++)
		numArray[i] = getNumFromBox(S, numArray[i]);

	result = arrayToInt(numArray);
	return result ^ Rcon[round];
}

//扩展密钥
static int w[44];

/**
 * 扩展密钥，结果是把w[44]中的每个元素初始化
 */
static void extendKey(const char *key) {
	int i, j;
	// 前4个复制
	for(i = 0; i < 4; i++){
		int word = 0x00000000;
		for(j = 0; j < 4; j++){
			int theChar = charToInt(key[4*i + j]);
			word = word | theChar<<(24 - j*8);
		}
		w[i] = word;
	}

	// 对之后的所有
	int round = 0;
	for(i = 4; i < 44; i++){
		if( i % 4 == 0) {	//要进行T操作
			w[i] = w[i - 4] ^ T(w[i - 1], round);
			round++;	//轮数+1
		}else {		//只与前一个有关
			w[i] = w[i - 4] ^ w[i - 1];
		}
	}

}

/**
 * 轮密钥加
 */
static void addKey(int (&array)[4][4], int round) {
	int warray[4];
	int i,j;
	for(i = 0; i < 4; i++) {
		intToArray(w[round*4 + i], warray);
		for(j = 0; j < 4; j++) {
			array[j][i] = array[j][i] ^ warray[j];
		}
	}
}

/**
 * 字节代换
 */
static void subBytes(int (&array)[4][4]){
	int i,j;
	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			array[i][j] = getNumFromBox(S, array[i][j]);
}

/**
 * 行移位（循环左移）
 */
static void shiftRows(int (&array)[4][4]) {
	int i,j;
	int copy[4];
	for(i=1; i<4; i++){
		// 记录下来
		for(j=0; j<4; j++){
			copy[j] = array[i][j];
		}
		for(j=0; j<4; j++){
			// 循环左移 i 位
			array[i][j] = copy[(4+j+i)%4];
		}
	}
}

static int GFMul2(int s) {
	int result = s << 1;
	int a7 = result & 0x00000100;

	if(a7 != 0) {
		result = result & 0x000000ff;
		result = result ^ 0x1b;
	}

	return result;
}

static int GFMul3(int s) {
	return GFMul2(s) ^ s;
}

static int GFMul4(int s) {
	return GFMul2(GFMul2(s));
}

static int GFMul8(int s) {
	return GFMul2(GFMul4(s));
}

static int GFMul9(int s) {
	return GFMul8(s) ^ s;
}

static int GFMul11(int s) {
	return GFMul9(s) ^ GFMul2(s);
}

static int GFMul12(int s) {
	return GFMul8(s) ^ GFMul4(s);
}

static int GFMul13(int s) {
	return GFMul12(s) ^ s;
}

static int GFMul14(int s) {
	return GFMul12(s) ^ GFMul2(s);
}

/**
 * GF上的二元运算
 */
static int GFMul(int n, int s) {
	int result;

	if(n == 1)
		result = s;
	else if(n == 2)
		result = GFMul2(s);
	else if(n == 3)
		result = GFMul3(s);
	else if(n == 0x9)
		result = GFMul9(s);
	else if(n == 0xb)//11
		result = GFMul11(s);
	else if(n == 0xd)//13
		result = GFMul13(s);
	else if(n == 0xe)//14
		result = GFMul14(s);

	return result;
}
/**
 * 列混合要用到的矩阵
 */
static const int colM[4][4] = { 
	2, 3, 1, 1,
	1, 2, 3, 1,
	1, 1, 2, 3,
	3, 1, 1, 2 };
/**
 * 列混合
 */
static void mixColumns(int (&array)[4][4]) {

	int copy[4][4];
	int i,j;
	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			copy[i][j] = array[i][j];

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++){
			array[i][j] = GFMul(colM[i][0],copy[0][j]) ^ GFMul(colM[i][1],copy[1][j])
				^ GFMul(colM[i][2],copy[2][j]) ^ GFMul(colM[i][3], copy[3][j]);
		}
}

/**
 * 把4X4数组转回字符串
 */
static void arrayToStr(int (&array)[4][4], char *str) {
	int i,j;
	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			*str++ = (char)array[j][i];
}

/**
 * 参数 p: 明文的字符串数组。
 * 参数 key: 密钥的字符串数组。
 */
void aes(char *p, const char *key){

	int pArray[4][4];
	int i;

	extendKey(key);//扩展密钥

    // 开始加密
    strToArray(p, pArray);
    addKey(pArray, 0);//一开始的轮密钥加

    // 中间九轮
    for(i = 1; i < 10; i++){
        subBytes(pArray);//字节代换
        shiftRows(pArray);//行移位
        mixColumns(pArray);//列混合
        addKey(pArray, i);
    }

    // 最后一轮
    subBytes(pArray);//字节代换
    shiftRows(pArray);//行移位
    addKey(pArray, 10);

    arrayToStr(pArray, p);
}

#include<time.h>

/**
 * 计数器+1
 */
static void ctrAdd(char *ctr) {
	int i=15;
	while (i>=0 && ++ctr[i]==0)
		i--;
}
// /**
//  * 拷贝16字节
//  */
// static void strCopy(char *dest, char* source) {
// 	int i;
// 	for(i=0;i<16;i++)
// 		dest[i] = source[i];
// }
/**
 * 测试用
 */
void showStr(char *str){
	int i;
	for(i=0;i<16;i++)
		printf("%02x", (unsigned char)str[i]);
		// printf("%c", (unsigned char)str[i]);
	printf("\n");
}

//计算y=x*H, H不变, x变；所以把H做成表：
static unsigned char hh[128][16]; //存储h,h<<1,h<<2,....h<<127
/**
 * 计算hh表
 */
int compute_hh(unsigned char (&h)[16])
{//计算h<<1，h<<2，h<<3，....h<<127；p(x)=x128+ x7 + x2 +x + 1; 
		int i,msb,j;    
	memcpy(hh[0],h,16);//hh[0]=h
	for(i=1;i<128;i++)
	{
		msb=hh[i-1][0]>>7;//最高位
		for(j=0;j<15;j++)//h[i]=h[i-1]<<1 | ...
			hh[i][j] = ((hh[i-1][j]<<1) | (hh[i-1][j+1]>>7)) &255;
		hh[i][15] = hh[i-1][15]<<1;
		if(msb==1)
			hh[i][15] ^= 0x87;
	}
	return 0;
}   
/**
 * 计算GF域上b=a*h
 */ 
static int mult_h(unsigned char (&a)[16],unsigned char (&b)[16])
{//有限域乘法G(2128);注意：a，b不能为同一个地址（b在不停的更新）
	int i,j,k,m;
	memset(b,0,16);
	for(k=0,i=15; i>=0; i--)//从低位开始，数据都是从高位开始的，所以从15开始。
	{//k为从低位开始的计数器
		for(j=0; j<8; j++,k++)
		{
			if( ((a[i]>>j)&1) == 1)//每个字节也从低位开始
			{
				for(m=0;m<16;m++)
					b[m] ^= hh[k][m] ;
			}
		}
	}
	return 0;
}

typedef unsigned char Uchar16[16];

int encode(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], char (&random)[16], char (&mac)[16]) {
	if(src_size == 0 || src_size % 16 != 0) {
		// printf("data len should be 16*n");
		LogHelper::log(Error, "src len should be 16*n, but src_size: %d", src_size);
		return -1;
	}
	if (dst_size < src_size) {
		LogHelper::log(Error, "dst_size should be at least the same as src_size, but dst_size: %, src_size: %d", dst_size, src_size);
		return -2;
	}

	memcpy(dst, src, src_size);

	srand((unsigned int)time((time_t *)NULL));
	// 生成随机计数器初始值
	// char* copy = (char*) malloc((16) * sizeof(char));
	char copy[16];
	int i, j;
	for(i=0; i<16; i++){
		copy[i] = random[i] = (char)rand();
	}
	// 计算h
	// char* h = (char*) malloc((16) * sizeof(char));
	char h[16];
	// strCopy(h, copy);
	memcpy(h, copy, 16);
	aes(h, key);

	// 加密 len/16 组数据，得到密文
	char ctr[16];
	for(i=0; i< (src_size >> 4); i++){
		ctrAdd(copy);	// 计数器 +1
		// char* ctr = (char*) malloc((16) * sizeof(char));
		memcpy(ctr, copy, sizeof(ctr));
		// strCopy(ctr, copy);
		aes(ctr, key);
		// 对该组数据的16个字节分别进行异或操作
		for(j=0; j<16; j++){
			dst[(i << 4) + j] = dst[ (i << 4) + j] ^ ctr[j];
		}
	}

	// 计算 mac 值
	compute_hh((Uchar16 &)h);
	// 第一次GF乘法
	mult_h((Uchar16 &)random, (Uchar16 &)mac);
	// 对每组密文进行一次
	char lastResult[16];
	for(i=0; i< (src_size >> 4); i++){
		// char* lastResult = (char*) malloc((16) * sizeof(char));
		// strCopy(lastResult, mac);
		memcpy(lastResult, mac, sizeof(lastResult));
		// 对该组密文的16个字节分别进行异或操作
		for(j=0; j<16; j++){
			lastResult[j] = lastResult[j] ^ dst[ (i << 4) + j];
		}
		mult_h((Uchar16 &)lastResult, (Uchar16 &)mac);
	}
	// 与h进行异或操作
	for(j=0; j<16; j++){
		mac[j] = mac[j] ^ h[j];
	}
	// printf("finish encode\n");
	LogHelper::log(Debug, "finish encode");
	return 0;
}

int decode(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], const char (&random)[16], const char (&mac)[16]) {
	// 利用计数器初始值计算h
	// char* copy = (char*) malloc((16) * sizeof(char));

	if(src_size == 0 || src_size % 16 != 0) {
		// printf("data len should be 16*n");
		LogHelper::log(Error, "src len should be 16*n, but src_size: %d", src_size);
		return -1;
	}

	if (dst_size < src_size) {
		LogHelper::log(Error, "dst_size should be at least the same as src_size, but dst_size: %, src_size: %d", dst_size, src_size);
		return -2;
	}
	memcpy(dst, src, src_size);

	char copy[16];
	// strCopy(copy, random);
	memcpy(copy, random, sizeof(copy));
	int i, j;
	// 计算h
	// char* h = (char*) malloc((16) * sizeof(char));
	char h[16];
	// strCopy(h, copy);
	memcpy(h, copy, sizeof(h));
	aes(h, key);

	// mac 值校验
	compute_hh((Uchar16 &)h);
	// char* result = (char*) malloc((16) * sizeof(char));
	char result[16];
	// 第一次GF乘法
	mult_h((Uchar16 &)random, (Uchar16 &)result);
	
	char lastResult[16];
	// 对每组密文进行一次
	for(i=0; i < (src_size >> 4); i++){
		// char* lastResult = (char*) malloc((16) * sizeof(char));
		// strCopy(lastResult, result);
		memcpy(lastResult, result, sizeof(lastResult));
		// 对该组密文的16个字节分别进行异或操作
		for(j=0; j<16; j++){
			lastResult[j] = lastResult[j] ^ dst[ (i << 4) + j];
		}
		mult_h((Uchar16 &)lastResult, (Uchar16 &)result);
	}
	// 与h进行异或操作
	for(j=0; j<16; j++){
		result[j] = result[j] ^ h[j];
	}
	// 校对
	for(j=0; j<16; j++){
		if(result[j] != mac[j]){
			// printf("mac error!\n");
			LogHelper::log(Warn, "mac check error");
			// exit(0);
			return -3;
		}
	}
	// printf("mac check ok\n");
	LogHelper::log(Debug, "mac check ok");

	char ctr[16];
	// 解密 len/16 组数据，得到数据
	for(i=0; i< (src_size >> 4) ; i++){
		ctrAdd(copy);	// 计数器 +1
		// char* ctr = (char*) malloc((16) * sizeof(char));
		// strCopy(ctr, copy);
		memcpy(ctr, copy, sizeof(ctr));
		aes(ctr, key);
		// 对该组数据的16个字节分别进行异或操作
		for(j=0; j<16; j++){
			dst[(i << 4) + j] = dst[(i << 4) + j] ^ ctr[j];
		}
	}
	// printf("finish decode\n");
	
	return 0;
}

int encrypt(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], char (&random)[16], char (&mac)[16]) {
	int valid_size = (src_size >> 4) << 4;
	if (valid_size < src_size)
		valid_size += 16;
	if (valid_size < src_size + 2) {
		valid_size += 16;
	}
	// static char buf[constants::AES_MAX_DATA_LENGTH];
	
	if (valid_size > constants::AES_MAX_DATA_LENGTH) {
		LogHelper::log(Error, "data size should be no longer than: %lu, but size is: %d", constants::AES_MAX_DATA_LENGTH, valid_size);
		return -5;
	}
	
	if (dst_size < valid_size) {
		LogHelper::log(Error, "dst_size is less than valid size, dst_size: %d, valid_size: %d", dst_size, valid_size);
		return -5;
	}
	char *buf = new char[valid_size];
	memcpy(buf, src, src_size);
	
	//Padding
	memset(buf + src_size, 0, valid_size - src_size);
	buf[src_size] = 0xf;

	int retCode = encode(dst, dst_size, buf, valid_size, key, random, mac);
	delete[] buf;
	if (retCode != 0) {
		LogHelper::log(Error, "Fail to encrypt");
		return retCode;
	}
	
	return valid_size;
}

int decrypt(char *dst, int dst_size, const char *src, int src_size, const char (&key)[16], const char (&random)[16], const char (&mac)[16]) {
	if (src_size > constants::AES_MAX_DATA_LENGTH) {
		LogHelper::log(Error, "src_size size should be no longer than: %lu, but size is: %d", constants::AES_MAX_DATA_LENGTH, src_size);
		return -5;
	}
	if (dst_size < src_size - 16) {
		LogHelper::log(Error, "dst size too small, dst size: %d, src size: %d", dst_size, src_size);
		return -6;
	}
	char *buf = new char[src_size];
	int deRet = decode(buf, src_size, src, src_size, key, random, mac);
	if (deRet != 0) {
		delete[] buf;
		LogHelper::log(Error, "Fail to decrypt");
		return deRet;
	}
	int real_size = src_size;
	while (buf[real_size - 1] == 0 && buf[real_size - 2] != 0xf) {
		real_size--;
	}
	if (buf[real_size - 2] != 0xf) {
		LogHelper::log(Error, "Padding error");
		delete[] buf;
		return -7;
	}
	real_size -= 2;
	if (dst_size < real_size) {
		LogHelper::log(Error, "dst_size less than real size, dist_size: %d, real_size: %d",dst_size, real_size);
		delete[] buf;
		return -8;
	}
	memcpy(dst, buf, real_size);
	delete[] buf;
	return real_size;
}

// Test for debug
int testEncodeAndDecode() {
	char key[16], random[16], mac[16];
	for (int i = 0; i < 16; ++i) {
		key[i] = rand() & 0xff;
	}
	char text[30] = "12345678901234566543210987654";
	for (int i = 0; i < sizeof(text) - 1; i++) {
		text[i] -= '0';
	}
	char buf[60] = {0};
	int codeLen = encrypt(buf, sizeof(buf), text, sizeof(text) - 1, key, random, mac);
	if (codeLen < 0) {
		printf("encode error, ret :%d", codeLen);
		return codeLen;
	}
	printf("after encode:\n");
	
	for(int i = 0; i < codeLen; ++i) {
		printf("%u ", buf[i]);
	}
	printf("\n");
	memset(text, 0, sizeof(text));
	// memset(random, 0, 2);
	codeLen = decrypt(text, sizeof(text), buf, codeLen, key, random, mac);
	if (codeLen < 0) {
		printf("decode error, ret :%d", codeLen);
		return codeLen;
	}
	printf("after decode:\n");
	for(int i = 0; i < codeLen; ++i) {
		printf("%u ", text[i]);
	}
	printf("\n");
	return 0;
}

// int main() {
// 	testEncodeAndDecode();
// }