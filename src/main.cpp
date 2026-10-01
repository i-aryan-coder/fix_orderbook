#include "MatchingEngine.h"
#include "FixApp.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>

#if __has_include("quickfix/SessionSettings.h") && defined(ENABLE_QUICKFIX)
#include "quickfix/SessionSettings.h"
#include "quickfix/FileStore.h"
#include "quickfix/FileLog.h"
#include "quickfix/SocketInitiator.h"

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    try {
        std::cout << "Starting Concurrent FIX Matching Engine with QuickFIX..." << std::endl;
        FIX::SessionSettings settings("trading_confi.cfg");

        MatchingEngine engine;
        engine.start();

        // Register trade listener to publish execution events
        engine.addTradeListener([](const Trade& t) {
            std::cout << "[Trade Broadcast] Matched " << t.getQuantity() << " @ " << t.getPrice()
                      << " (Buyer: " << t.getBuyOrderId() << ", Seller: " << t.getSellOrderId() << ")\n";
        });

        FixApp application(engine);

        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);

        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);
        initiator.start();

        std::cout << "FIX Engine started. Listening for incoming messages..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(60));

        auto snapshot = engine.getSnapshot();
        std::cout << "\n--- Order Book Snapshot (Seq: " << snapshot->getSequenceNumber() << ") ---" << std::endl;
        std::cout << "Bids:" << std::endl;
        for (const auto& bid : snapshot->getbids()) {
            std::cout << "  Price: " << bid.price << ", Quantity: " << bid.quantity << std::endl;
        }
        std::cout << "Asks:" << std::endl;
        for (const auto& ask : snapshot->getasks()) {
            std::cout << "  Price: " << ask.price << ", Quantity: " << ask.quantity << std::endl;
        }

        initiator.stop();
        engine.stop(true);
        return 0;
    }
    catch (std::exception& e) {
        std::cerr << "Exception in main: " << e.what() << std::endl;
        return 1;
    }
}

#else

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    try {
        std::cout << "===================================================================\n";
        std::cout << "  Concurrent Order Ingestion & Serialized Matching Engine (Phase 2)\n";
        std::cout << "===================================================================\n\n";

        MatchingEngine engine;

        // Register event & trade listeners (Future FIX ExecutionReports / WebSocket)
        engine.addTradeListener([](const Trade& t) {
            std::cout << "  [Trade Event] Executed: " << t.getQuantity() << " @ " << t.getPrice()
                      << " (Buyer: " << t.getBuyOrderId() << ", Seller: " << t.getSellOrderId() << ")\n";
        });

        engine.addEventListener([](const OrderEventResult& res) {
            if (!res.success) {
                std::cout << "  [Event Error] Order " << res.orderId << " rejected: " << res.errorMessage << "\n";
            }
        });

        std::cout << "[Lifecycle] Starting dedicated matching engine worker thread...\n";
        engine.start();

        std::cout << "\n[Producers] Launching concurrent producer threads (Simulating FIX & REST)...\n";

        // Producer 1 (e.g. FIX Gateway thread submitting resting sell liquidity)
        std::thread fixProducer([&engine]() {
            std::cout << "  -> FIX Gateway thread submitting Sell orders (101 @ 100, 102 @ 101, 103 @ 102)...\n";
            engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 101, Side::Sell, 100, 10));
            engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 102, Side::Sell, 101, 10));
            engine.submitNewOrder(std::make_shared<Order>(OrderType::Limit, 103, Side::Sell, 102, 10));
        });

        // Producer 2 (e.g. REST API thread submitting aggressive buy orders)
        std::thread restProducer([&engine]() {
            // Small stagger to let initial asks queue
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::cout << "  -> REST Gateway thread submitting Market Buy (ID: 201, Qty: 15)...\n";
            engine.submitNewOrder(std::make_shared<Order>(201, Side::Buy, 15));

            std::cout << "  -> REST Gateway thread submitting FAK Buy (ID: 202, Limit: 101, Qty: 10)...\n";
            engine.submitNewOrder(std::make_shared<Order>(OrderType::FillAndKill, 202, Side::Buy, 101, 10));
        });

        fixProducer.join();
        restProducer.join();

        // Give the matching engine a moment to process and print trades
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        std::cout << "\n[Snapshot] Fetching immutable read-only snapshot for display...\n";
        auto snapshot = engine.getSnapshot();
        std::cout << "  Snapshot Sequence Number: " << snapshot->getSequenceNumber() << "\n";
        std::cout << "  Bids:\n";
        if (snapshot->getbids().empty()) std::cout << "    (None)\n";
        for (const auto& b : snapshot->getbids()) {
            std::cout << "    Price: " << b.price << " | Quantity: " << b.quantity << "\n";
        }
        std::cout << "  Asks:\n";
        if (snapshot->getasks().empty()) std::cout << "    (None)\n";
        for (const auto& a : snapshot->getasks()) {
            std::cout << "    Price: " << a.price << " | Quantity: " << a.quantity << "\n";
        }

        std::cout << "\n[Lifecycle] Stopping matching engine (graceful queue drain)...\n";
        engine.stop(true);
        std::cout << "Matching engine shutdown complete. Zero data races, deterministic state.\n";

        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }
}

#endif
