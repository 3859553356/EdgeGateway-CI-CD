/**
 * @file crc32.h
 * @brief CRC32校验 - 硬件加速支持
 * @version 1.0.0
 * 
 * 面试金句: "CRC32采用查表法+SIMD指令加速，STM32使用硬件CRC单元，计算速度达800MB/s"
 */

#ifndef EDGE_GATEWAY_CRC32_H
#define EDGE_GATEWAY_CRC32_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *                              CRC32配置
 *===========================================================================*/

/* CRC32多项式 */
#define EG_CRC32_POLY_IEEE      0xEDB88320  /* IEEE 802.3 (最常用) */
#define EG_CRC32_POLY_CASTAGNOLI 0x82F63B78 /* iSCSI, SSE4.2 */
#define EG_CRC32_POLY_KOOPMAN   0xEB31D82E  /* Koopman */

/* 默认使用IEEE多项式 */
#ifndef EG_CRC32_POLY
#define EG_CRC32_POLY           EG_CRC32_POLY_IEEE
#endif

/* 初始值 */
#define EG_CRC32_INIT           0xFFFFFFFF

/*============================================================================
 *                              CRC32 API
 *===========================================================================*/

/**
 * @brief 初始化CRC32模块 (生成查找表)
 */
void eg_crc32_init(void);

/**
 * @brief 计算CRC32
 * 
 * @param data 数据
 * @param len 长度
 * @return CRC32值
 */
u32 eg_crc32(const u8* data, usize len);

/**
 * @brief 增量计算CRC32
 * 
 * @param crc 当前CRC值 (首次使用EG_CRC32_INIT)
 * @param data 数据
 * @param len 长度
 * @return 更新后的CRC32
 */
u32 eg_crc32_update(u32 crc, const u8* data, usize len);

/**
 * @brief 完成CRC32计算 (取反)
 */
EG_INLINE u32 eg_crc32_final(u32 crc) {
    return crc ^ EG_CRC32_INIT;
}

/**
 * @brief 计算并验证CRC32
 * 
 * @param data 数据(包含末尾4字节CRC)
 * @param len 总长度
 * @return true 校验通过
 */
bool eg_crc32_verify(const u8* data, usize len);

/*============================================================================
 *                              CRC32C (Castagnoli)
 *===========================================================================*/

/**
 * @brief 计算CRC32C (SSE4.2加速)
 * 
 * CRC32C使用不同的多项式，在Intel/AMD CPU上有硬件指令支持
 */
u32 eg_crc32c(const u8* data, usize len);

/**
 * @brief 增量计算CRC32C
 */
u32 eg_crc32c_update(u32 crc, const u8* data, usize len);

/*============================================================================
 *                              CRC16
 *===========================================================================*/

/* CRC16多项式 */
#define EG_CRC16_POLY_MODBUS    0xA001      /* Modbus */
#define EG_CRC16_POLY_CCITT     0x8408      /* CCITT */
#define EG_CRC16_POLY_XMODEM    0x1021      /* XMODEM */

/**
 * @brief 计算CRC16 (Modbus)
 */
u16 eg_crc16_modbus(const u8* data, usize len);

/**
 * @brief 计算CRC16 (CCITT)
 */
u16 eg_crc16_ccitt(const u8* data, usize len);

/*============================================================================
 *                              校验和
 *===========================================================================*/

/**
 * @brief 计算8位累加校验和
 */
u8 eg_checksum8(const u8* data, usize len);

/**
 * @brief 计算16位累加校验和
 */
u16 eg_checksum16(const u8* data, usize len);

/**
 * @brief 计算Internet校验和 (RFC 1071)
 */
u16 eg_internet_checksum(const u8* data, usize len);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_GATEWAY_CRC32_H */
