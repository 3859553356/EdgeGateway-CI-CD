# EdgeGateway Makefile
# 统一构建系统 - 支持嵌入式(STM32H7)和Linux双平台
#
# 面试金句: "统一Makefile实现双平台构建，一键切换DEBUG/RELEASE，CI/CD全自动化"
#
# 使用方法:
#   make linux          - 构建Linux版本
#   make embedded       - 构建嵌入式版本
#   make test           - 运行单元测试
#   make coverage       - 生成覆盖率报告
#   make static-check   - 静态检查
#   make flash          - 烧录固件
#   make clean          - 清理构建产物

# ============================================================================
#                              项目配置
# ============================================================================

PROJECT_NAME    := edge-gateway
VERSION         := 1.0.0

# 目录结构
SRC_DIR         := src
INC_DIR         := include
BUILD_DIR       := build
BIN_DIR         := bin
TEST_DIR        := tests

# 公共源文件
COMMON_SRC      := $(wildcard $(SRC_DIR)/common/*.c)
COMMON_OBJ      := $(COMMON_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)

# ============================================================================
#                              编译器配置
# ============================================================================

# Linux编译器
CC_LINUX        := gcc
CXX_LINUX       := g++

# ARM嵌入式编译器
CC_ARM          := arm-none-eabi-gcc
CXX_ARM         := arm-none-eabi-g++
OBJCOPY         := arm-none-eabi-objcopy
SIZE            := arm-none-eabi-size

# ============================================================================
#                              编译标志
# ============================================================================

# 通用警告标志 (零警告目标)
WARN_FLAGS      := -Wall -Wextra -Wpedantic -Werror \
                   -Wformat=2 -Wformat-security \
                   -Wnull-dereference -Wdouble-promotion \
                   -Wshadow -Wundef -Wcast-align \
                   -Wstrict-overflow=5 -Wwrite-strings \
                   -Wconversion -Wsign-conversion

# 优化标志
OPT_DEBUG       := -O0 -g3 -DDEBUG
OPT_RELEASE     := -O3 -DNDEBUG -flto

# Linux编译标志
CFLAGS_LINUX    := -std=c11 -D_GNU_SOURCE $(WARN_FLAGS) \
                   -I$(INC_DIR) \
                   -fPIC -fstack-protector-strong \
                   -D_FORTIFY_SOURCE=2

LDFLAGS_LINUX   := -lpthread -lm -lrt

# ARM编译标志
CPU_FLAGS       := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
CFLAGS_ARM      := -std=c11 $(CPU_FLAGS) $(WARN_FLAGS) \
                   -I$(INC_DIR) \
                   -DSTM32H743xx -DUSE_HAL_DRIVER -DEG_PLATFORM_EMBEDDED \
                   -ffunction-sections -fdata-sections -fno-common

LDFLAGS_ARM     := $(CPU_FLAGS) \
                   -T$(SRC_DIR)/embedded/STM32H743ZITx_FLASH.ld \
                   -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/$(PROJECT_NAME).map \
                   --specs=nano.specs --specs=nosys.specs

# 调试/发布配置
ifdef DEBUG
    CFLAGS_LINUX += $(OPT_DEBUG)
    CFLAGS_ARM   += $(OPT_DEBUG)
else
    CFLAGS_LINUX += $(OPT_RELEASE)
    CFLAGS_ARM   += $(OPT_RELEASE)
endif

# Sanitizer (仅Linux调试)
ifdef ASAN
    CFLAGS_LINUX  += -fsanitize=address,undefined
    LDFLAGS_LINUX += -fsanitize=address,undefined
endif

# ============================================================================
#                              Linux构建目标
# ============================================================================

LINUX_SRC       := $(COMMON_SRC) \
                   $(wildcard $(SRC_DIR)/linux/**/*.c)
LINUX_OBJ       := $(LINUX_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/linux/%.o)
LINUX_BIN       := $(BIN_DIR)/$(PROJECT_NAME)

.PHONY: linux
linux: $(LINUX_BIN)
	@echo "Linux build complete: $<"

$(LINUX_BIN): $(LINUX_OBJ) | $(BIN_DIR)
	$(CC_LINUX) $(CFLAGS_LINUX) -o $@ $^ $(LDFLAGS_LINUX)

$(BUILD_DIR)/linux/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC_LINUX) $(CFLAGS_LINUX) -c -o $@ $<

# ============================================================================
#                              嵌入式构建目标
# ============================================================================

# STM32 HAL库路径
STM32_HAL       := $(SRC_DIR)/embedded/drivers/STM32H7xx_HAL_Driver
FREERTOS        := $(SRC_DIR)/embedded/rtos/FreeRTOS

ARM_SRC         := $(COMMON_SRC) \
                   $(wildcard $(SRC_DIR)/embedded/**/*.c) \
                   $(wildcard $(STM32_HAL)/Src/*.c) \
                   $(wildcard $(FREERTOS)/*.c)

ARM_OBJ         := $(ARM_SRC:$(SRC_DIR)/%.c=$(BUILD_DIR)/arm/%.o)
ARM_ELF         := $(BIN_DIR)/$(PROJECT_NAME).elf
ARM_BIN         := $(BIN_DIR)/$(PROJECT_NAME).bin
ARM_HEX         := $(BIN_DIR)/$(PROJECT_NAME).hex

CFLAGS_ARM      += -I$(STM32_HAL)/Inc \
                   -I$(FREERTOS)/include \
                   -I$(FREERTOS)/portable/GCC/ARM_CM7/r0p1

.PHONY: embedded
embedded: $(ARM_BIN) $(ARM_HEX)
	@echo "Embedded build complete"
	$(SIZE) $(ARM_ELF)

$(ARM_ELF): $(ARM_OBJ) | $(BIN_DIR)
	$(CC_ARM) $(LDFLAGS_ARM) -o $@ $^

$(ARM_BIN): $(ARM_ELF)
	$(OBJCOPY) -O binary $< $@

$(ARM_HEX): $(ARM_ELF)
	$(OBJCOPY) -O ihex $< $@

$(BUILD_DIR)/arm/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC_ARM) $(CFLAGS_ARM) -c -o $@ $<

# ============================================================================
#                              测试目标
# ============================================================================

TEST_SRC        := $(wildcard $(TEST_DIR)/*.c) \
                   $(wildcard $(TEST_DIR)/unity/*.c)
TEST_OBJ        := $(TEST_SRC:$(TEST_DIR)/%.c=$(BUILD_DIR)/test/%.o)
TEST_BIN        := $(BIN_DIR)/test_runner

CFLAGS_TEST     := $(CFLAGS_LINUX) -I$(TEST_DIR)/unity -DTEST_BUILD

.PHONY: test
test: $(TEST_BIN)
	@echo "Running tests..."
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_OBJ) $(filter-out $(BUILD_DIR)/linux/linux/server/epoll_server.o, $(LINUX_OBJ)) | $(BIN_DIR)
	$(CC_LINUX) $(CFLAGS_TEST) -o $@ $^ $(LDFLAGS_LINUX)

$(BUILD_DIR)/test/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC_LINUX) $(CFLAGS_TEST) -c -o $@ $<

# ============================================================================
#                              覆盖率
# ============================================================================

.PHONY: coverage
coverage: CFLAGS_LINUX += --coverage -fprofile-arcs -ftest-coverage
coverage: LDFLAGS_LINUX += --coverage
coverage: clean test
	@echo "Generating coverage report..."
	lcov --capture --directory $(BUILD_DIR) --output-file $(BUILD_DIR)/coverage.info
	lcov --remove $(BUILD_DIR)/coverage.info '/usr/*' '*/tests/*' --output-file $(BUILD_DIR)/coverage.info
	genhtml $(BUILD_DIR)/coverage.info --output-directory $(BUILD_DIR)/coverage_report
	@echo "Coverage report: $(BUILD_DIR)/coverage_report/index.html"

# ============================================================================
#                              静态检查
# ============================================================================

.PHONY: static-check
static-check:
	@echo "Running cppcheck..."
	cppcheck --enable=all --std=c11 --suppress=missingIncludeSystem \
		-I$(INC_DIR) $(SRC_DIR) 2>&1 | tee $(BUILD_DIR)/cppcheck.log
	@echo "Running clang-format check..."
	find $(SRC_DIR) $(INC_DIR) -name '*.c' -o -name '*.h' | \
		xargs clang-format -style=file --dry-run -Werror 2>&1 || \
		(echo "Format check failed. Run 'make format' to fix." && exit 1)

.PHONY: format
format:
	@echo "Formatting code..."
	find $(SRC_DIR) $(INC_DIR) -name '*.c' -o -name '*.h' | \
		xargs clang-format -style=file -i

# ============================================================================
#                              烧录目标
# ============================================================================

OPENOCD_CFG     := interface/stlink.cfg
OPENOCD_TARGET  := target/stm32h7x.cfg

.PHONY: flash
flash: $(ARM_BIN)
	@echo "Flashing firmware..."
	openocd -f $(OPENOCD_CFG) -f $(OPENOCD_TARGET) \
		-c "program $(ARM_BIN) 0x08000000 verify reset exit"

.PHONY: debug
debug: $(ARM_ELF)
	@echo "Starting GDB debug session..."
	openocd -f $(OPENOCD_CFG) -f $(OPENOCD_TARGET) &
	arm-none-eabi-gdb $(ARM_ELF) \
		-ex "target remote localhost:3333" \
		-ex "monitor reset halt" \
		-ex "load"

# ============================================================================
#                              性能测试
# ============================================================================

.PHONY: benchmark
benchmark: linux
	@echo "Running benchmark..."
	./scripts/benchmark.sh

.PHONY: valgrind
valgrind: DEBUG=1
valgrind: linux
	@echo "Running Valgrind memory check..."
	valgrind --leak-check=full --show-leak-kinds=all \
		--track-origins=yes --verbose \
		--log-file=$(BUILD_DIR)/valgrind.log \
		$(LINUX_BIN)

# ============================================================================
#                              Docker
# ============================================================================

.PHONY: docker-build
docker-build:
	docker build -t $(PROJECT_NAME):$(VERSION) .

.PHONY: docker-run
docker-run:
	docker run -p 9000:9000 $(PROJECT_NAME):$(VERSION)

# ============================================================================
#                              清理
# ============================================================================

.PHONY: clean
clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

.PHONY: distclean
distclean: clean
	rm -rf *.log

# ============================================================================
#                              帮助
# ============================================================================

.PHONY: help
help:
	@echo "EdgeGateway Build System"
	@echo ""
	@echo "Usage: make [target] [options]"
	@echo ""
	@echo "Targets:"
	@echo "  linux         Build Linux version"
	@echo "  embedded      Build STM32H7 embedded version"
	@echo "  test          Run unit tests"
	@echo "  coverage      Generate code coverage report"
	@echo "  static-check  Run static analysis"
	@echo "  format        Format source code"
	@echo "  flash         Flash firmware to STM32"
	@echo "  debug         Start GDB debug session"
	@echo "  benchmark     Run performance benchmark"
	@echo "  valgrind      Run memory check"
	@echo "  docker-build  Build Docker image"
	@echo "  clean         Clean build artifacts"
	@echo ""
	@echo "Options:"
	@echo "  DEBUG=1       Enable debug build"
	@echo "  ASAN=1        Enable AddressSanitizer (Linux only)"
	@echo ""
	@echo "Examples:"
	@echo "  make linux DEBUG=1      - Debug Linux build"
	@echo "  make embedded           - Release embedded build"
	@echo "  make test ASAN=1        - Test with sanitizer"

# 创建目录
$(BUILD_DIR) $(BIN_DIR):
	mkdir -p $@

# 依赖文件
-include $(LINUX_OBJ:.o=.d)
-include $(ARM_OBJ:.o=.d)
-include $(TEST_OBJ:.o=.d)
