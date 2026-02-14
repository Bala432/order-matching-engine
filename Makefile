CXX := g++

# --------------------------------------------------
# Common flags (shared by all builds)
# --------------------------------------------------
COMMON_FLAGS := -std=c++2a -pthread -DNDEBUG -Iinclude -Ibench

# --------------------------------------------------
# Optimization profiles
# --------------------------------------------------
RELEASE_FLAGS := -O2
PERF_FLAGS    := -O3 -march=native
DEBUG_FLAGS   := -O0 -g
SAN_FLAGS := -fsanitize=address -fno-omit-frame-pointer

# --------------------------------------------------
# Source files
# --------------------------------------------------
SRC := src/Orderbook.cpp

CORRECTNESS_SRC := src/orderbook_correctness.cpp
BENCH_SRC       := src/benchmark_main.cpp

# --------------------------------------------------
# Output binaries
# --------------------------------------------------
CORRECTNESS_OUT := ob_correctness.exe
BENCH_OUT       := ome_benchmark.exe
BENCH_DEBUG_OUT := ome_benchmark_debug.exe


# --------------------------------------------------
# Targets
# --------------------------------------------------
.PHONY: all correctness bench clean

all: correctness

# --------------------------------------------------
# Correctness unit tests (lightweight, assert-based)
# --------------------------------------------------
correctness: $(CORRECTNESS_SRC) $(SRC)
	$(CXX) $(COMMON_FLAGS) $(RELEASE_FLAGS) $^ -o $(CORRECTNESS_OUT)
	@echo "Built correctness test: $(CORRECTNESS_OUT)"

# --------------------------------------------------
# Benchmark binary (used for correctness + perf modes)
# --------------------------------------------------
bench: $(BENCH_SRC) $(SRC)
	$(CXX) $(COMMON_FLAGS) $(PERF_FLAGS) $^ -o $(BENCH_OUT)
	@echo "Built benchmark binary: $(BENCH_OUT)"
	@echo "Run:"
	@echo "  ./$(BENCH_OUT) --mode=correctness --events"
	@echo "  ./$(BENCH_OUT) --mode=perf"

bench_debug: $(BENCH_SRC) $(SRC)
	$(CXX) $(COMMON_FLAGS) $(DEBUG_FLAGS) $(SAN_FLAGS) $^ -o $(BENCH_DEBUG_OUT)
	@echo "Built DEBUG benchmark with ASAN: $(BENCH_DEBUG_OUT)"
	@echo "Run:"
	@echo "  ./$(BENCH_DEBUG_OUT) --mode=correctness --events"

# --------------------------------------------------
# Cleanup
# --------------------------------------------------
clean:
	rm -f *.exe
