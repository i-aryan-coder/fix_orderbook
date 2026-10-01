CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -Iinclude

SRC_DIR = src
INC_DIR = include
TEST_DIR = tests

ENGINE_BIN = engine.exe
TEST_BIN = run_tests.exe
CONC_TEST_BIN = run_concurrency_tests.exe
FIX_TEST_BIN = run_fix_tests.exe
API_TEST_BIN = run_api_tests.exe
API_SERVER_BIN = api_server.exe
BENCH_BIN = run_benchmarks.exe
TSAN_CONC_TEST_BIN = run_tsan_concurrency_tests.exe

ifeq ($(OS),Windows_NT)
SOCKET_LIBS = -lws2_32
else
SOCKET_LIBS =
endif

ENGINE_SRCS = $(SRC_DIR)/Orderbook.cpp $(SRC_DIR)/MatchingEngine.cpp $(SRC_DIR)/FixApp.cpp $(SRC_DIR)/main.cpp
API_SRCS = $(SRC_DIR)/ApiModels.cpp $(SRC_DIR)/ApiJson.cpp $(SRC_DIR)/TradeStore.cpp $(SRC_DIR)/OrderStatusStore.cpp $(SRC_DIR)/OutboundBroadcastQueue.cpp $(SRC_DIR)/WebSocketBroadcaster.cpp $(SRC_DIR)/ApiServer.cpp
NETWORK_SRCS = $(SRC_DIR)/HttpApiServer.cpp
TEST_SRCS = $(SRC_DIR)/Orderbook.cpp $(TEST_DIR)/OrderbookTests.cpp
CONC_TEST_SRCS = $(SRC_DIR)/Orderbook.cpp $(SRC_DIR)/MatchingEngine.cpp $(TEST_DIR)/ConcurrencyTests.cpp
FIX_TEST_SRCS = $(SRC_DIR)/Orderbook.cpp $(SRC_DIR)/MatchingEngine.cpp $(SRC_DIR)/FixApp.cpp $(TEST_DIR)/FixAppTests.cpp
API_TEST_SRCS = $(SRC_DIR)/Orderbook.cpp $(SRC_DIR)/MatchingEngine.cpp $(SRC_DIR)/FixApp.cpp $(API_SRCS) $(NETWORK_SRCS) $(TEST_DIR)/ApiTests.cpp
API_SERVER_SRCS = $(SRC_DIR)/Orderbook.cpp $(SRC_DIR)/MatchingEngine.cpp $(API_SRCS) $(NETWORK_SRCS) $(SRC_DIR)/api_main.cpp
BENCH_SRCS = $(SRC_DIR)/Orderbook.cpp $(SRC_DIR)/MatchingEngine.cpp $(TEST_DIR)/Benchmarks.cpp
TSAN_CONC_TEST_SRCS = $(CONC_TEST_SRCS)
TSAN_FLAGS ?= -std=c++17 -g -O1 -fsanitize=thread -fno-omit-frame-pointer -Iinclude

.PHONY: all api-server benchmark test test-orderbook test-concurrency test-fix test-api test-concurrency-tsan clean

all: $(ENGINE_BIN) $(API_SERVER_BIN)

$(ENGINE_BIN): $(ENGINE_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

api-server: $(API_SERVER_BIN)

$(API_SERVER_BIN): $(API_SERVER_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(SOCKET_LIBS)

benchmark: $(BENCH_BIN)
	./$(BENCH_BIN)

test: test-orderbook test-concurrency test-fix test-api

test-orderbook: $(TEST_BIN)
	./$(TEST_BIN)

test-concurrency: $(CONC_TEST_BIN)
	./$(CONC_TEST_BIN)

test-fix: $(FIX_TEST_BIN)
	./$(FIX_TEST_BIN)

test-api: $(API_TEST_BIN)
	./$(API_TEST_BIN)

$(TEST_BIN): $(TEST_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(CONC_TEST_BIN): $(CONC_TEST_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(FIX_TEST_BIN): $(FIX_TEST_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

$(API_TEST_BIN): $(API_TEST_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(SOCKET_LIBS)

$(BENCH_BIN): $(BENCH_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

test-concurrency-tsan: $(TSAN_CONC_TEST_BIN)
	./$(TSAN_CONC_TEST_BIN)

$(TSAN_CONC_TEST_BIN): $(TSAN_CONC_TEST_SRCS)
	$(CXX) $(TSAN_FLAGS) $^ -o $@ -fsanitize=thread

clean:
	rm -f $(ENGINE_BIN) $(API_SERVER_BIN) $(TEST_BIN) $(CONC_TEST_BIN) $(FIX_TEST_BIN) $(API_TEST_BIN) $(BENCH_BIN) $(TSAN_CONC_TEST_BIN) $(SRC_DIR)/*.o
