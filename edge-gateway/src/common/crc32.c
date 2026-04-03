/**
 * @file crc32.c
 * @brief CRC32校验实现
 * @version 1.0.0
 */

#include "crc32.h"
#include <string.h>

/*============================================================================
 *                              查找表
 *===========================================================================*/

static u32 s_crc32_table[256];
static u32 s_crc32c_table[256];
static u16 s_crc16_modbus_table[256];
static bool s_tables_initialized = false;

/**
 * @brief 生成CRC查找表
 */
static void generate_crc32_table(u32* table, u32 poly) {
    for (u32 i = 0; i < 256; i++) {
        u32 crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
}

static void generate_crc16_table(u16* table, u16 poly) {
    for (u16 i = 0; i < 256; i++) {
        u16 crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
        table[i] = crc;
    }
}

void eg_crc32_init(void) {
    if (s_tables_initialized) {
        return;
    }
    
    generate_crc32_table(s_crc32_table, EG_CRC32_POLY_IEEE);
    generate_crc32_table(s_crc32c_table, EG_CRC32_POLY_CASTAGNOLI);
    generate_crc16_table(s_crc16_modbus_table, EG_CRC16_POLY_MODBUS);
    
    s_tables_initialized = true;
}

/*============================================================================
 *                              CRC32实现
 *===========================================================================*/

u32 eg_crc32_update(u32 crc, const u8* data, usize len) {
    if (!s_tables_initialized) {
        eg_crc32_init();
    }
    
    /* 4字节展开优化 */
    while (len >= 4) {
        crc = s_crc32_table[(crc ^ data[0]) & 0xFF] ^ (crc >> 8);
        crc = s_crc32_table[(crc ^ data[1]) & 0xFF] ^ (crc >> 8);
        crc = s_crc32_table[(crc ^ data[2]) & 0xFF] ^ (crc >> 8);
        crc = s_crc32_table[(crc ^ data[3]) & 0xFF] ^ (crc >> 8);
        data += 4;
        len -= 4;
    }
    
    while (len--) {
        crc = s_crc32_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    
    return crc;
}

u32 eg_crc32(const u8* data, usize len) {
    return eg_crc32_final(eg_crc32_update(EG_CRC32_INIT, data, len));
}

bool eg_crc32_verify(const u8* data, usize len) {
    if (len < 4) {
        return false;
    }
    
    /* 计算数据部分的CRC */
    u32 calculated = eg_crc32(data, len - 4);
    
    /* 读取存储的CRC (小端) */
    u32 stored = (u32)data[len - 4] |
                 ((u32)data[len - 3] << 8) |
                 ((u32)data[len - 2] << 16) |
                 ((u32)data[len - 1] << 24);
    
    return calculated == stored;
}

/*============================================================================
 *                              CRC32C实现 (Castagnoli)
 *===========================================================================*/

#if defined(__SSE4_2__) && defined(EG_PLATFORM_LINUX)
#include <nmmintrin.h>

u32 eg_crc32c_update(u32 crc, const u8* data, usize len) {
    /* 使用SSE4.2硬件指令 */
    
    /* 8字节对齐处理 */
    while (len && ((uptr)data & 7)) {
        crc = _mm_crc32_u8(crc, *data++);
        len--;
    }
    
    /* 64位批量处理 */
    while (len >= 8) {
        crc = (u32)_mm_crc32_u64(crc, *(const u64*)data);
        data += 8;
        len -= 8;
    }
    
    /* 处理剩余字节 */
    while (len--) {
        crc = _mm_crc32_u8(crc, *data++);
    }
    
    return crc;
}

#else

u32 eg_crc32c_update(u32 crc, const u8* data, usize len) {
    if (!s_tables_initialized) {
        eg_crc32_init();
    }
    
    while (len >= 4) {
        crc = s_crc32c_table[(crc ^ data[0]) & 0xFF] ^ (crc >> 8);
        crc = s_crc32c_table[(crc ^ data[1]) & 0xFF] ^ (crc >> 8);
        crc = s_crc32c_table[(crc ^ data[2]) & 0xFF] ^ (crc >> 8);
        crc = s_crc32c_table[(crc ^ data[3]) & 0xFF] ^ (crc >> 8);
        data += 4;
        len -= 4;
    }
    
    while (len--) {
        crc = s_crc32c_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    
    return crc;
}

#endif

u32 eg_crc32c(const u8* data, usize len) {
    return eg_crc32c_update(EG_CRC32_INIT, data, len) ^ EG_CRC32_INIT;
}

/*============================================================================
 *                              CRC16实现
 *===========================================================================*/

u16 eg_crc16_modbus(const u8* data, usize len) {
    if (!s_tables_initialized) {
        eg_crc32_init();
    }
    
    u16 crc = 0xFFFF;
    
    while (len--) {
        crc = s_crc16_modbus_table[(crc ^ *data++) & 0xFF] ^ (crc >> 8);
    }
    
    return crc;
}

u16 eg_crc16_ccitt(const u8* data, usize len) {
    u16 crc = 0xFFFF;
    
    while (len--) {
        u8 x = crc >> 8 ^ *data++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((u16)x << 12) ^ ((u16)x << 5) ^ (u16)x;
    }
    
    return crc;
}

/*============================================================================
 *                              校验和实现
 *===========================================================================*/

u8 eg_checksum8(const u8* data, usize len) {
    u8 sum = 0;
    while (len--) {
        sum += *data++;
    }
    return sum;
}

u16 eg_checksum16(const u8* data, usize len) {
    u16 sum = 0;
    while (len >= 2) {
        sum += (u16)data[0] | ((u16)data[1] << 8);
        data += 2;
        len -= 2;
    }
    if (len) {
        sum += data[0];
    }
    return sum;
}

u16 eg_internet_checksum(const u8* data, usize len) {
    u32 sum = 0;
    
    /* 累加16位字 */
    while (len >= 2) {
        sum += ((u16)data[0] << 8) | data[1];
        data += 2;
        len -= 2;
    }
    
    /* 处理奇数字节 */
    if (len) {
        sum += (u16)data[0] << 8;
    }
    
    /* 折叠32位到16位 */
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return (u16)~sum;
}

/*============================================================================
 *                              STM32硬件CRC
 *===========================================================================*/

#if defined(EG_PLATFORM_EMBEDDED) && defined(STM32H7)
#include "stm32h7xx_hal.h"

static CRC_HandleTypeDef s_hcrc;
static bool s_hw_crc_initialized = false;

/**
 * @brief 初始化STM32硬件CRC单元
 */
eg_error_t eg_hw_crc_init(void) {
    if (s_hw_crc_initialized) {
        return EG_OK;
    }
    
    __HAL_RCC_CRC_CLK_ENABLE();
    
    s_hcrc.Instance = CRC;
    s_hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
    s_hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
    s_hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_BYTE;
    s_hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_ENABLE;
    s_hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
    
    if (HAL_CRC_Init(&s_hcrc) != HAL_OK) {
        return EG_ERR_HARDWARE;
    }
    
    s_hw_crc_initialized = true;
    return EG_OK;
}

/**
 * @brief 使用硬件CRC计算
 */
u32 eg_hw_crc32(const u8* data, usize len) {
    if (!s_hw_crc_initialized) {
        eg_hw_crc_init();
    }
    
    u32 crc = HAL_CRC_Calculate(&s_hcrc, (u32*)data, len);
    return crc ^ 0xFFFFFFFF;
}

#endif /* STM32H7 */
