#!/bin/bash
# EdgeGateway 固件烧录脚本
# 支持OpenOCD/ST-Link/J-Link

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
FIRMWARE="${PROJECT_DIR}/bin/edge-gateway.bin"
ELF_FILE="${PROJECT_DIR}/bin/edge-gateway.elf"

# 颜色
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

# 检查固件文件
check_firmware() {
    if [ ! -f "$FIRMWARE" ]; then
        log_error "Firmware not found: $FIRMWARE"
        log_info "Building firmware..."
        cd "$PROJECT_DIR"
        make embedded
    fi
    
    log_info "Firmware size: $(stat -c%s "$FIRMWARE") bytes"
}

# OpenOCD烧录
flash_openocd() {
    log_info "Flashing with OpenOCD..."
    
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32h7x.cfg \
        -c "program $FIRMWARE 0x08000000 verify reset exit"
    
    log_info "Flash complete!"
}

# ST-Link工具烧录
flash_stlink() {
    log_info "Flashing with ST-Link..."
    
    st-flash write "$FIRMWARE" 0x08000000
    
    log_info "Flash complete!"
}

# J-Link烧录
flash_jlink() {
    log_info "Flashing with J-Link..."
    
    # 创建JLink脚本
    cat > /tmp/jlink_flash.jlink << EOF
device STM32H743ZI
si SWD
speed 4000
connect
r
h
loadbin $FIRMWARE, 0x08000000
verifybin $FIRMWARE, 0x08000000
r
g
q
EOF
    
    JLinkExe -CommandFile /tmp/jlink_flash.jlink
    rm -f /tmp/jlink_flash.jlink
    
    log_info "Flash complete!"
}

# 擦除Flash
erase_flash() {
    log_info "Erasing flash..."
    
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32h7x.cfg \
        -c "init" \
        -c "reset halt" \
        -c "stm32h7x mass_erase 0" \
        -c "exit"
    
    log_info "Erase complete!"
}

# 验证固件
verify_firmware() {
    log_info "Verifying firmware..."
    
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32h7x.cfg \
        -c "init" \
        -c "reset halt" \
        -c "verify_image $FIRMWARE 0x08000000" \
        -c "exit"
    
    log_info "Verification complete!"
}

# 读取固件
read_firmware() {
    OUTPUT="${1:-firmware_dump.bin}"
    SIZE="${2:-1048576}"  # 默认1MB
    
    log_info "Reading firmware to $OUTPUT..."
    
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32h7x.cfg \
        -c "init" \
        -c "reset halt" \
        -c "dump_image $OUTPUT 0x08000000 $SIZE" \
        -c "exit"
    
    log_info "Read complete: $(stat -c%s "$OUTPUT") bytes"
}

# 启动GDB调试
start_gdb() {
    log_info "Starting GDB debug session..."
    
    # 启动OpenOCD服务器
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32h7x.cfg &
    OPENOCD_PID=$!
    sleep 2
    
    # 启动GDB
    arm-none-eabi-gdb "$ELF_FILE" \
        -ex "target remote localhost:3333" \
        -ex "monitor reset halt" \
        -ex "load" \
        -ex "break main" \
        -ex "continue"
    
    # 清理
    kill $OPENOCD_PID 2>/dev/null || true
}

# 重置设备
reset_device() {
    log_info "Resetting device..."
    
    openocd \
        -f interface/stlink.cfg \
        -f target/stm32h7x.cfg \
        -c "init" \
        -c "reset run" \
        -c "exit"
    
    log_info "Reset complete!"
}

# 显示帮助
show_help() {
    cat << EOF
EdgeGateway Firmware Flash Tool

Usage: $0 [command] [options]

Commands:
  flash       Flash firmware (default: OpenOCD)
  flash-st    Flash using ST-Link utility
  flash-jlink Flash using J-Link
  erase       Erase flash memory
  verify      Verify flashed firmware
  read        Read firmware from device
  gdb         Start GDB debug session
  reset       Reset device
  help        Show this help

Options:
  --firmware PATH   Specify firmware file (default: bin/edge-gateway.bin)

Examples:
  $0 flash                    # Flash with OpenOCD
  $0 flash-jlink              # Flash with J-Link
  $0 read dump.bin 524288     # Read 512KB firmware

EOF
}

# 主函数
main() {
    case "${1:-flash}" in
        flash)      check_firmware && flash_openocd ;;
        flash-st)   check_firmware && flash_stlink ;;
        flash-jlink) check_firmware && flash_jlink ;;
        erase)      erase_flash ;;
        verify)     check_firmware && verify_firmware ;;
        read)       read_firmware "$2" "$3" ;;
        gdb)        start_gdb ;;
        reset)      reset_device ;;
        help|--help|-h) show_help ;;
        *)
            log_error "Unknown command: $1"
            show_help
            exit 1
            ;;
    esac
}

main "$@"
