CXX := g++
CXXFLAGS := -std=c++20 -O2 -Iinclude \
            -Igoogletest/googletest/include \
            -Igoogletest/googlemock/include \
            -Igoogletest/googletest -pthread -DNDEBUG

# Ultra-aggressive flags only for benchmark
# Aggressive flags for perf, BUT leave exceptions enabled
# PERF_FLAGS := -O3 -march=native -funroll-loops -DNDEBUG -pthread
PERF_FLAGS := -O0 -g -pthread

# Sources
SRC := src/Orderbook.cpp         # add more if you have them
BENCH_SRC := src/benchmark_main.cpp             # exact location

# Outputs
TEST_OUT     := test_ome.exe
MAIN_OUT     := ome_main.exe
BENCH_OUT    := ome_benchmark.exe

.PHONY: all test run bench clean

all: test

# Tests
$(TEST_OUT): tests/test_load.cpp $(SRC) googletest/googletest/src/gtest-all.cc
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(TEST_OUT)
	./$(TEST_OUT) --gtest_color=yes

# Normal main
$(MAIN_OUT): src/main.cpp $(SRC)
	$(CXX) $(CXXFLAGS) $^ -o $@

run: $(MAIN_OUT)
	./$(MAIN_OUT)

# BENCHMARK — the one that matters
$(BENCH_OUT): $(BENCH_SRC) $(SRC)
	$(CXX) $(CXXFLAGS) $(PERF_FLAGS) -Iinclude -Ibench $^ -o $@

bench: $(BENCH_OUT)
	@echo "Benchmark binary built: $(BENCH_OUT)"

clean:
	-del /Q *.exe 2>nul || rm -f *.exe