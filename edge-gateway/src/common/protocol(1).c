/**
 * @file protocol.c
 * @brief 协议实现
 * @version 1.0.0
 */

#include "protocol.h"
#include "crc32.h"
#include "memory_pool.h"
#include <string.h>

/*============================================================================
 *                              解析器实现
 *===========================================================================*/

eg_error_t eg_proto_parser_init(eg_proto_parser_t* parser, u8* buffer, usize buf_size) {
    if (!parser) {
        return EG_ERR_INVALID_PARAM;
    }
    
    memset(parser, 0, sizeof(eg_proto_parser_t));
    parser->state = EG_PARSER_IDLE;
    
    if (buffer && buf_size > 0) {
        parser->payload_buf = buffer;
    } else {
        parser->payload_buf = (u8*)eg_malloc(EG_PROTO_MAX_PAYLOAD);
        if (!parser->payload_buf) {
            return EG_ERR_NOMEM;
        }
    }
    
    return EG_OK;
}

void eg_proto_parser_reset(eg_proto_parser_t* parser) {
    if (!parser) return;
    
    parser->state = EG_PARSER_IDLE;
    parser->header_pos = 0;
    parser->payload_pos = 0;
    parser->payload_len = 0;
    parser->crc_pos = 0;
    memset(&parser->frame, 0, sizeof(eg_proto_frame_t));
}

void eg_proto_parser_destroy(eg_proto_parser_t* parser) {
    if (parser && parser->payload_buf) {
        eg_free(parser->payload_buf);
        parser->payload_buf = NULL;
    }
}

/**
 * @brief 查找魔数
 */
static const u8* find_magic(const u8* data, usize len) {
    for (usize i = 0; i + 1 < len; i++) {
        if (data[i] == 0x47 && data[i + 1] == 0x45) {  /* "GE" little-endian */
            return &data[i];
        }
    }
    return NULL;
}

eg_parser_state_t eg_proto_parse(eg_proto_parser_t* parser,
                                  const u8* data, usize len,
                                  usize* consumed) {
    if (!parser || !data || !consumed) {
        return EG_PARSER_ERROR;
    }
    
    *consumed = 0;
    const u8* ptr = data;
    usize remaining = len;
    
    while (remaining > 0) {
        switch (parser->state) {
            case EG_PARSER_IDLE:
            case EG_PARSER_HEADER: {
                /* 解析头部 */
                while (parser->header_pos < EG_PROTO_HEADER_LEN && remaining > 0) {
                    parser->header_buf[parser->header_pos++] = *ptr++;
                    remaining--;
                    (*consumed)++;
                }
                
                if (parser->header_pos >= EG_PROTO_HEADER_LEN) {
                    /* 解析头部结构 */
                    eg_proto_header_t* hdr = &parser->frame.header;
                    hdr->magic = *(u16*)&parser->header_buf[0];
                    hdr->version = parser->header_buf[2];
                    hdr->flags = parser->header_buf[3];
                    hdr->msg_type = *(u16*)&parser->header_buf[4];
                    hdr->payload_len = *(u16*)&parser->header_buf[6];
                    
                    /* 验证头部 */
                    if (hdr->magic != EG_PROTO_MAGIC_VALUE) {
                        parser->frames_error++;
                        eg_proto_parser_reset(parser);
                        return EG_PARSER_ERROR;
                    }
                    
                    if (hdr->payload_len > EG_PROTO_MAX_PAYLOAD) {
                        parser->frames_error++;
                        eg_proto_parser_reset(parser);
                        return EG_PARSER_ERROR;
                    }
                    
                    parser->payload_len = hdr->payload_len;
                    parser->payload_pos = 0;
                    parser->state = EG_PARSER_PAYLOAD;
                }
                break;
            }
            
            case EG_PARSER_PAYLOAD: {
                /* 解析负载 */
                while (parser->payload_pos < parser->payload_len && remaining > 0) {
                    parser->payload_buf[parser->payload_pos++] = *ptr++;
                    remaining--;
                    (*consumed)++;
                }
                
                if (parser->payload_pos >= parser->payload_len) {
                    parser->frame.payload = parser->payload_buf;
                    parser->crc_pos = 0;
                    parser->state = EG_PARSER_CRC;
                }
                break;
            }
            
            case EG_PARSER_CRC: {
                /* 解析CRC */
                while (parser->crc_pos < EG_PROTO_CRC_LEN && remaining > 0) {
                    parser->crc_buf[parser->crc_pos++] = *ptr++;
                    remaining--;
                    (*consumed)++;
                }
                
                if (parser->crc_pos >= EG_PROTO_CRC_LEN) {
                    /* 验证CRC */
                    u32 received_crc = *(u32*)parser->crc_buf;
                    
                    /* 计算CRC: header + payload */
                    u32 calc_crc = eg_crc32_update(EG_CRC32_INIT, 
                                                    parser->header_buf, 
                                                    EG_PROTO_HEADER_LEN);
                    calc_crc = eg_crc32_update(calc_crc,
                                               parser->payload_buf,
                                               parser->payload_len);
                    calc_crc = eg_crc32_final(calc_crc);
                    
                    if (calc_crc != received_crc) {
                        parser->frames_error++;
                        eg_proto_parser_reset(parser);
                        return EG_PARSER_ERROR;
                    }
                    
                    parser->frame.crc = received_crc;
                    parser->frames_parsed++;
                    parser->state = EG_PARSER_COMPLETE;
                    return EG_PARSER_COMPLETE;
                }
                break;
            }
            
            case EG_PARSER_COMPLETE:
                /* 已完成，需要调用reset */
                return EG_PARSER_COMPLETE;
                
            case EG_PARSER_ERROR:
            default:
                return EG_PARSER_ERROR;
        }
    }
    
    parser->bytes_received += *consumed;
    return parser->state;
}

eg_proto_frame_t* eg_proto_get_frame(eg_proto_parser_t* parser) {
    if (parser && parser->state == EG_PARSER_COMPLETE) {
        return &parser->frame;
    }
    return NULL;
}

/*============================================================================
 *                              帧构建
 *===========================================================================*/

usize eg_proto_build_frame(u8* buffer, usize buf_size,
                            u16 msg_type, u8 flags,
                            const u8* payload, u16 payload_len) {
    usize frame_len = eg_proto_frame_len(payload_len);
    
    if (!buffer || buf_size < frame_len) {
        return 0;
    }
    
    /* 构建头部 */
    buffer[0] = 0x47;  /* 'G' */
    buffer[1] = 0x45;  /* 'E' */
    buffer[2] = EG_PROTO_VERSION;
    buffer[3] = flags;
    *(u16*)&buffer[4] = msg_type;
    *(u16*)&buffer[6] = payload_len;
    
    /* 复制负载 */
    if (payload && payload_len > 0) {
        memcpy(buffer + EG_PROTO_HEADER_LEN, payload, payload_len);
    }
    
    /* 计算CRC */
    u32 crc = eg_crc32(buffer, EG_PROTO_HEADER_LEN + payload_len);
    *(u32*)&buffer[EG_PROTO_HEADER_LEN + payload_len] = crc;
    
    return frame_len;
}

usize eg_proto_build_heartbeat(u8* buffer, usize buf_size) {
    return eg_proto_build_frame(buffer, buf_size, 
                                 EG_MSG_HEARTBEAT, 
                                 EG_PROTO_FLAG_HEARTBEAT,
                                 NULL, 0);
}

usize eg_proto_build_heartbeat_ack(u8* buffer, usize buf_size) {
    return eg_proto_build_frame(buffer, buf_size,
                                 EG_MSG_HEARTBEAT_ACK,
                                 EG_PROTO_FLAG_HEARTBEAT | EG_PROTO_FLAG_ACK,
                                 NULL, 0);
}

usize eg_proto_build_data(u8* buffer, usize buf_size,
                           const eg_data_point_t* points, u16 count) {
    if (!points || count == 0) {
        return 0;
    }
    
    u16 payload_len = count * sizeof(eg_data_point_t);
    if (payload_len > EG_PROTO_MAX_PAYLOAD) {
        return 0;
    }
    
    return eg_proto_build_frame(buffer, buf_size,
                                 EG_MSG_DATA_BATCH,
                                 EG_PROTO_FLAG_NONE,
                                 (const u8*)points, payload_len);
}

bool eg_proto_verify_frame(const u8* frame, usize len) {
    if (!frame || len < EG_PROTO_MIN_FRAME) {
        return false;
    }
    
    /* 检查魔数 */
    if (frame[0] != 0x47 || frame[1] != 0x45) {
        return false;
    }
    
    /* 获取负载长度 */
    u16 payload_len = *(u16*)&frame[6];
    usize expected_len = eg_proto_frame_len(payload_len);
    
    if (len != expected_len) {
        return false;
    }
    
    /* 验证CRC */
    u32 stored_crc = *(u32*)&frame[len - EG_PROTO_CRC_LEN];
    u32 calc_crc = eg_crc32(frame, len - EG_PROTO_CRC_LEN);
    
    return calc_crc == stored_crc;
}

u16 eg_proto_get_payload_len(const u8* header) {
    if (!header) return 0;
    return *(u16*)&header[6];
}
