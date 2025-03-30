#include "Orderbook.h"
#include "FixApp.h"
#include "quickfix/SessionSettings.h"
#include "quickfix/FileStore.h"
#include "quickfix/FileLog.h"
#include "quickfix/SocketInitiator.h"
#include <iostream>
#include <thread>
#include <chrono>

int main(int argc, char** argv) {
    try {
        // Load QuickFIX configuration.
        FIX::SessionSettings settings("trading_confi.cfg");

        // Instantiate the order matching engine.
        Orderbook orderbook;

        // Create our FIX application.
        FixApp application(orderbook);

        // Set up store and log factories.
        FIX::FileStoreFactory storeFactory(settings);
        FIX::FileLogFactory logFactory(settings);

        // Create the FIX initiator (client).
        FIX::SocketInitiator initiator(application, storeFactory, settings, logFactory);
        initiator.start();

        std::cout << "FIX Engine started. Waiting for orders..." << std::endl;

        // For demonstration, run for 60 seconds to receive orders.
        std::this_thread::sleep_for(std::chrono::seconds(60));

        // Optionally, print the aggregated order book.
        AggregatedOrderbook aggbook = orderbook.getorderinfo();
        std::cout << "\n--- Aggregated Order Book ---" << std::endl;
        std::cout << "Bids:" << std::endl;
        for (const auto& bid : aggbook.getbids()) {
            std::cout << "Price: " << bid.price << ", Quantity: " << bid.quantity << std::endl;
        }
        std::cout << "Asks:" << std::endl;
        for (const auto& ask : aggbook.getasks()) {
            std::cout << "Price: " << ask.price << ", Quantity: " << ask.quantity << std::endl;
        }

        initiator.stop();
        return 0;
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }
}
