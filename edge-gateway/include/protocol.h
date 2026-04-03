/**
 * @file protocol.h
 * @brief 自研二进制协议 - 高性能通信
 * @version 1.0.0
 * 
 * 面试金句: "自研协议采用定长Header+变长Payload设计，解析零拷贝，单包处理延迟<50µs"
 * 
 * 协议格式:
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |  Magic(2B)  | Ver(1B) | Flags(1B) |      Length(4B)      |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |                      Payload (N Bytes)                               |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 * |                      CRC32 (4 Bytes)                                 |
 * +--------+--------+--------+--------+--------+--------+--------+--------+
 */

#ifndef EDGE_GATEWAY_PROTOCOL_H
#define EDGE_GATEWAY_PROTOCOL_H

#include "types.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*============================================================================
 *                              协议常量
 *===========================================================================*/

#define EG_PROTO_MAGIC_BYTE1    0xED
#define EG_PROTO_MAGIC_BYTE2    0xGE
#define EG_PROTO_MAGIC_VALUE    0x4547  /* "GE" in little-endian */
#define EG_PROTO_VERSION        0x01

/* 帧结构 */
#define EG_PROTO_HEADER_LEN     8
#define EG_PROTO_CRC_LEN        4
#define EG_PROTO_MIN_FRAME      (EG_PROTO_HEADER_LEN + EG_PROTO_CRC_LEN)
#define EG_PROTO_MAX_PAYLOAD    4096
#define EG_PROTO_MAX_FRAME      (EG_PROTO_HEADER_LEN + EG_PROTO_MAX_PAYLOAD + EG_PROTO_CRC_LEN)

/*============================================================================
 *                              标志位定义
 *===========================================================================*/

typedef enum eg_proto_flags {
    EG_PROTO_FLAG_NONE      = 0x00,
    EG_PROTO_FLAG_ACK       = 0x01,     /* 需要应答 */
    EG_PROTO_FLAG_COMPRESS  = 0x02,     /* 数据压缩 */
    EG_PROTO_FLAG_ENCRYPT   = 0x04,     /* 数据加密 */
    EG_PROTO_FLAG_FRAGMENT  = 0x08,     /* 分片标志 */
    EG_PROTO_FLAG_LAST_FRAG = 0x10,     /* 最后分片 */
    EG_PROTO_FLAG_PRIORITY  = 0x20,     /* 高优先级 */
    EG_PROTO_FLAG_HEARTBEAT = 0x40,     /* 心跳包 */
    EG_PROTO_FLAG_ERROR     = 0x80,     /* 错误响应 */
} eg_proto_flags_t;

/*============================================================================
 *                              消息类型
 *===========================================================================*/

typedef enum eg_msg_type {
    /* 系统消息 0x00-0x0F */
    EG_MSG_HEARTBEAT        = 0x00,     /* 心跳 */
    EG_MSG_HEARTBEAT_ACK    = 0x01,     /* 心跳应答 */
    EG_MSG_CONNECT          = 0x02,     /* 连接请求 */
    EG_MSG_CONNECT_ACK      = 0x03,     /* 连接应答 */
    EG_MSG_DISCONNECT       = 0x04,     /* 断开请求 */
    EG_MSG_ERROR            = 0x0F,     /* 错误通知 */
    
    /* 数据消息 0x10-0x3F */
    EG_MSG_DATA_SINGLE      = 0x10,     /* 单条数据 */
    EG_MSG_DATA_BATCH       = 0x11,     /* 批量数据 */
    EG_MSG_DATA_ACK         = 0x12,     /* 数据确认 */
    EG_MSG_DATA_RESEND      = 0x13,     /* 重传请求 */
    
    /* 控制消息 0x40-0x5F */
    EG_MSG_CMD_REQUEST      = 0x40,     /* 控制命令 */
    EG_MSG_CMD_RESPONSE     = 0x41,     /* 命令响应 */
    EG_MSG_CONFIG_GET       = 0x42,     /* 获取配置 */
    EG_MSG_CONFIG_SET       = 0x43,     /* 设置配置 */
    
    /* OTA消息 0x60-0x6F */
    EG_MSG_OTA_START        = 0x60,     /* OTA开始 */
    EG_MSG_OTA_DATA         = 0x61,     /* OTA数据 */
    EG_MSG_OTA_FINISH       = 0x62,     /* OTA完成 */
    EG_MSG_OTA_VERIFY       = 0x63,     /* OTA验证 */
    
    /* 告警消息 0x80-0x8F */
    EG_MSG_ALARM            = 0x80,     /* 告警上报 */
    EG_MSG_ALARM_ACK        = 0x81,     /* 告警确认 */
    EG_MSG_ALARM_CLEAR      = 0x82,     /* 告警清除 */
} eg_msg_type_t;

/*============================================================================
 *                              协议结构
 *===========================================================================*/

/**
 * @brief 协议头部 (8字节,紧凑排列)
 */
typedef struct EG_PACKED eg_proto_header {
    u16 magic;              /* 魔数 0x4547 */
    u8  version;            /* 协议版本 */
    u8  flags;              /* 标志位 */
    u16 msg_type;           /* 消息类型 */
    u16 payload_len;        /* 负载长度 */
} eg_proto_header_t;

/**
 * @brief 完整协议帧
 */
typedef struct eg_proto_frame {
    eg_proto_header_t   header;
    u8*                 payload;        /* 负载指针 */
    u32                 crc;            /* CRC校验 */
    
    /* 元数据 (非传输) */
    u64                 timestamp;      /* 时间戳 */
    u32                 seq_num;        /* 序列号 */
    void*               user_data;      /* 用户数据 */
} eg_proto_frame_t;

/*============================================================================
 *                              常用负载结构
 *===========================================================================*/

/**
 * @brief 数据点
 */
typedef struct EG_PACKED eg_data_point {
    u32 tag_id;             /* 测点ID */
    u64 timestamp;          /* 时间戳(ms) */
    u8  data_type;          /* 数据类型 */
    u8  quality;            /* 质量码 */
    u16 reserved;
    union {
        s32 i32_val;
        u32 u32_val;
        f32 f32_val;
        f64 f64_val;
        u8  bytes[8];
    } value;
} eg_data_point_t;

/**
 * @brief 告警信息
 */
typedef struct EG_PACKED eg_alarm_info {
    u32 alarm_id;
    u32 source_id;
    u64 timestamp;
    u8  severity;           /* 严重级别 1-5 */
    u8  state;              /* 状态: 产生/确认/清除 */
    u16 alarm_code;
    char message[64];
} eg_alarm_info_t;

/**
 * @brief 连接请求
 */
typedef struct EG_PACKED eg_connect_req {
    char device_id[32];     /* 设备ID */
    char firmware_ver[16];  /* 固件版本 */
    u32  capabilities;      /* 能力标志 */
    u32  keepalive_sec;     /* 心跳间隔 */
} eg_connect_req_t;

/*============================================================================
 *                              解析器
 *===========================================================================*/

/**
 * @brief 解析器状态
 */
typedef enum eg_parser_state {
    EG_PARSER_IDLE,         /* 空闲 */
    EG_PARSER_HEADER,       /* 解析头部 */
    EG_PARSER_PAYLOAD,      /* 解析负载 */
    EG_PARSER_CRC,          /* 校验CRC */
    EG_PARSER_COMPLETE,     /* 解析完成 */
    EG_PARSER_ERROR,        /* 解析错误 */
} eg_parser_state_t;

/**
 * @brief 流式解析器
 */
typedef struct eg_proto_parser {
    eg_parser_state_t   state;
    u8                  header_buf[EG_PROTO_HEADER_LEN];
    u32                 header_pos;
    
    u8*                 payload_buf;
    u32                 payload_pos;
    u32                 payload_len;
    
    u8                  crc_buf[EG_PROTO_CRC_LEN];
    u32                 crc_pos;
    
    eg_proto_frame_t    frame;          /* 当前帧 */
    
    /* 统计 */
    u64                 frames_parsed;
    u64                 frames_error;
    u64                 bytes_received;
} eg_proto_parser_t;

/*============================================================================
 *                              API
 *===========================================================================*/

/**
 * @brief 初始化解析器
 */
eg_error_t eg_proto_parser_init(eg_proto_parser_t* parser, u8* buffer, usize buf_size);

/**
 * @brief 重置解析器
 */
void eg_proto_parser_reset(eg_proto_parser_t* parser);

/**
 * @brief 销毁解析器
 */
void eg_proto_parser_destroy(eg_proto_parser_t* parser);

/**
 * @brief 输入数据进行解析
 * 
 * @param parser 解析器
 * @param data 输入数据
 * @param len 数据长度
 * @param consumed 输出已消费字节数
 * @return 解析状态
 */
eg_parser_state_t eg_proto_parse(eg_proto_parser_t* parser, 
                                  const u8* data, usize len,
                                  usize* consumed);

/**
 * @brief 获取已解析的帧
 */
eg_proto_frame_t* eg_proto_get_frame(eg_proto_parser_t* parser);

/**
 * @brief 构建协议帧
 * 
 * @param buffer 输出缓冲区
 * @param buf_size 缓冲区大小
 * @param msg_type 消息类型
 * @param flags 标志位
 * @param payload 负载数据
 * @param payload_len 负载长度
 * @return 帧总长度, 失败返回0
 */
usize eg_proto_build_frame(u8* buffer, usize buf_size,
                            u16 msg_type, u8 flags,
                            const u8* payload, u16 payload_len);

/**
 * @brief 快速构建心跳包
 */
usize eg_proto_build_heartbeat(u8* buffer, usize buf_size);

/**
 * @brief 快速构建心跳响应
 */
usize eg_proto_build_heartbeat_ack(u8* buffer, usize buf_size);

/**
 * @brief 快速构建数据包
 */
usize eg_proto_build_data(u8* buffer, usize buf_size,
                           const eg_data_point_t* points, u16 count);

/**
 * @brief 验证帧完整性
 */
bool eg_proto_verify_frame(const u8* frame, usize len);

/**
 * @brief 获取负载长度 (从原始数据)
 */
u16 eg_proto_get_payload_len(const u8* header);

/**
 * @brief 获取帧总长度
 */
EG_INLINE usize eg_proto_frame_len(u16 payload_len) {
    return EG_PROTO_HEADER_LEN + payload_len + EG_PROTO_CRC_LEN;
}

#ifdef __cplusplus
}
#endif

#endif /* EDGE_GATEWAY_PROTOCOL_H */
