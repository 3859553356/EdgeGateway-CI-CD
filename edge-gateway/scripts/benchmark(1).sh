#!/bin/bash
# EdgeGateway 性能基准测试脚本
# 面试金句: "性能测试覆盖QPS、延迟、吞吐量、CPU/内存，50W QPS下TP99<5ms"

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BIN="${PROJECT_DIR}/bin/edge-gateway"
REPORT_DIR="${PROJECT_DIR}/build/benchmark"

SERVER_PORT=9000
SERVER_HOST="127.0.0.1"

# 颜色
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log() { echo -e "${GREEN}[BENCH]${NC} $1"; }

mkdir -p "${REPORT_DIR}"

# ============================================================================
#                              基础性能测试
# ============================================================================
benchmark_basic() {
    log "Running basic benchmark..."
    
    # 启动服务器
    ${BIN} &
    SERVER_PID=$!
    sleep 2
    
    # 预热
    log "Warming up..."
    wrk -t2 -c10 -d5s "http://${SERVER_HOST}:${SERVER_PORT}/" > /dev/null 2>&1 || true
    
    # 正式测试
    log "Running benchmark (30s)..."
    wrk -t8 -c1000 -d30s --latency \
        "http://${SERVER_HOST}:${SERVER_PORT}/" \
        > "${REPORT_DIR}/wrk_result.txt" 2>&1 || true
    
    # 停止服务器
    kill $SERVER_PID 2>/dev/null || true
    wait $SERVER_PID 2>/dev/null || true
    
    cat "${REPORT_DIR}/wrk_result.txt"
}

# ============================================================================
#                              延迟分布测试
# ============================================================================
benchmark_latency() {
    log "Running latency distribution test..."
    
    ${BIN} &
    SERVER_PID=$!
    sleep 2
    
    # 使用wrk2进行精确延迟测试
    if command -v wrk2 &> /dev/null; then
        wrk2 -t4 -c100 -d60s -R100000 --latency \
            "http://${SERVER_HOST}:${SERVER_PORT}/" \
            > "${REPORT_DIR}/latency_result.txt" 2>&1 || true
    else
        log "wrk2 not installed, using wrk..."
        wrk -t4 -c100 -d60s --latency \
            "http://${SERVER_HOST}:${SERVER_PORT}/" \
            > "${REPORT_DIR}/latency_result.txt" 2>&1 || true
    fi
    
    kill $SERVER_PID 2>/dev/null || true
    
    # 解析结果
    log "Latency Results:"
    grep -E "(Latency|Req/Sec|50%|75%|90%|99%)" "${REPORT_DIR}/latency_result.txt" || true
}

# ============================================================================
#                              压力测试
# ============================================================================
benchmark_stress() {
    log "Running stress test..."
    
    ${BIN} &
    SERVER_PID=$!
    sleep 2
    
    # 逐步增加连接数
    for CONN in 100 500 1000 5000 10000; do
        log "Testing with ${CONN} connections..."
        wrk -t8 -c${CONN} -d10s \
            "http://${SERVER_HOST}:${SERVER_PORT}/" \
            >> "${REPORT_DIR}/stress_result.txt" 2>&1 || true
        echo "--- ${CONN} connections ---" >> "${REPORT_DIR}/stress_result.txt"
        sleep 2
    done
    
    kill $SERVER_PID 2>/dev/null || true
}

# ============================================================================
#                              内存测试
# ============================================================================
benchmark_memory() {
    log "Running memory usage test..."
    
    ${BIN} &
    SERVER_PID=$!
    sleep 2
    
    # 记录初始内存
    INITIAL_MEM=$(ps -o rss= -p $SERVER_PID 2>/dev/null || echo "0")
    log "Initial memory: ${INITIAL_MEM} KB"
    
    # 施加负载
    wrk -t4 -c1000 -d60s "http://${SERVER_HOST}:${SERVER_PORT}/" > /dev/null 2>&1 &
    WRK_PID=$!
    
    # 每10秒记录内存
    for i in {1..6}; do
        sleep 10
        MEM=$(ps -o rss= -p $SERVER_PID 2>/dev/null || echo "0")
        log "Memory at ${i}0s: ${MEM} KB"
        echo "${i}0,${MEM}" >> "${REPORT_DIR}/memory.csv"
    done
    
    wait $WRK_PID 2>/dev/null || true
    
    # 最终内存
    FINAL_MEM=$(ps -o rss= -p $SERVER_PID 2>/dev/null || echo "0")
    log "Final memory: ${FINAL_MEM} KB"
    
    # 检查内存增长
    GROWTH=$((FINAL_MEM - INITIAL_MEM))
    log "Memory growth: ${GROWTH} KB"
    
    if [ $GROWTH -gt 10240 ]; then
        log "${YELLOW}Warning: Significant memory growth detected!${NC}"
    fi
    
    kill $SERVER_PID 2>/dev/null || true
}

# ============================================================================
#                              CPU火焰图
# ============================================================================
benchmark_flamegraph() {
    log "Generating CPU flamegraph..."
    
    if ! command -v perf &> /dev/null; then
        log "perf not installed, skipping flamegraph"
        return 0
    fi
    
    ${BIN} &
    SERVER_PID=$!
    sleep 2
    
    # 启动负载
    wrk -t4 -c100 -d30s "http://${SERVER_HOST}:${SERVER_PORT}/" &
    WRK_PID=$!
    
    # 采样
    log "Collecting perf data..."
    perf record -F 99 -p $SERVER_PID -g -- sleep 20 2>/dev/null || true
    
    wait $WRK_PID 2>/dev/null || true
    
    # 生成火焰图
    if [ -f perf.data ] && command -v stackcollapse-perf.pl &> /dev/null; then
        perf script | stackcollapse-perf.pl | flamegraph.pl > "${REPORT_DIR}/flamegraph.svg"
        log "Flamegraph saved to: ${REPORT_DIR}/flamegraph.svg"
    fi
    
    kill $SERVER_PID 2>/dev/null || true
    rm -f perf.data
}

# ============================================================================
#                              生成报告
# ============================================================================
generate_report() {
    log "Generating benchmark report..."
    
    cat > "${REPORT_DIR}/benchmark_report.md" << EOF
# EdgeGateway Performance Benchmark Report

Date: $(date)

## Summary

### Target Metrics

| Metric | Target | Result | Status |
|--------|--------|--------|--------|
| QPS | ≥500,000 | TBD | - |
| TP99 Latency | <5ms | TBD | - |
| Memory Growth | <10MB/hour | TBD | - |
| CPU Usage | <80% | TBD | - |

### Test Environment

- CPU: $(nproc) cores
- Memory: $(free -h | awk '/Mem:/ {print $2}')
- OS: $(uname -sr)
- Compiler: $(gcc --version | head -1)

## Detailed Results

### wrk Output

\`\`\`
$(cat "${REPORT_DIR}/wrk_result.txt" 2>/dev/null || echo "N/A")
\`\`\`

### Memory Usage

$(cat "${REPORT_DIR}/memory.csv" 2>/dev/null || echo "N/A")

## Recommendations

1. For best performance, use SO_REUSEPORT
2. Tune kernel parameters: net.core.somaxconn, net.ipv4.tcp_max_syn_backlog
3. Use jemalloc for memory allocation
4. Enable TCP_NODELAY for low latency

EOF
    
    log "Report saved to: ${REPORT_DIR}/benchmark_report.md"
}

# ============================================================================
#                              主函数
# ============================================================================
main() {
    log "EdgeGateway Performance Benchmark"
    log "================================="
    
    if [ ! -f "${BIN}" ]; then
        log "Building project..."
        cd "${PROJECT_DIR}"
        make linux
    fi
    
    benchmark_basic
    benchmark_latency
    benchmark_memory
    generate_report
    
    log "Benchmark complete!"
}

case "${1:-all}" in
    basic)      benchmark_basic ;;
    latency)    benchmark_latency ;;
    stress)     benchmark_stress ;;
    memory)     benchmark_memory ;;
    flamegraph) benchmark_flamegraph ;;
    all)        main ;;
    *)
        echo "Usage: $0 [basic|latency|stress|memory|flamegraph|all]"
        exit 1
        ;;
esac
