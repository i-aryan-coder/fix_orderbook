#ifndef FIXAPP_H
#define FIXAPP_H

#include "Orderbook.h"
#include "quickfix/Application.h"
#include "quickfix/MessageCracker.h"
#include "quickfix/fix42/NewOrderSingle.h"
#include "quickfix/fix42/ExecutionReport.h"
#include <iostream>
#include <stdexcept>

class FixApp : public FIX::Application, public FIX::MessageCracker {
public:
    FixApp(Orderbook& ob) : orderbook_(ob) {}

    void onCreate(const FIX::SessionID& sessionID) override {
        std::cout << "Session created: " << sessionID << std::endl;
    }

    void onLogon(const FIX::SessionID& sessionID) override {
        std::cout << "Logon: " << sessionID << std::endl;
        sessionID_ = sessionID;
    }

    void onLogout(const FIX::SessionID& sessionID) override {
        std::cout << "Logout: " << sessionID << std::endl;
    }

    void fromAdmin(const FIX::Message&, const FIX::SessionID&) override {}
    void toAdmin(FIX::Message&, const FIX::SessionID&) override {}
    void toApp(FIX::Message& message, const FIX::SessionID& sessionID) override {
        // Optional: log outgoing messages.
    }

    void fromApp(const FIX::Message& message, const FIX::SessionID& sessionID) override {
        crack(message, sessionID);
    }

    // Handle NewOrderSingle messages.
    void onMessage(const FIX42::NewOrderSingle& orderMsg, const FIX::SessionID& sessionID) override {
        std::cout << "Received NewOrderSingle" << std::endl;
        FIX::ClOrdID clOrdID;
        FIX::Side side;
        FIX::Price price;
        FIX::OrderQty orderQty;
        orderMsg.get(clOrdID);
        orderMsg.get(side);
        orderMsg.get(price);
        orderMsg.get(orderQty);

        int id = std::stoi(clOrdID.getString());
        int pr = price.getValue();
        int qty = orderQty.getValue();
        Side s = (side.getValue() == FIX::Side_BUY) ? Side::Buy : Side::Sell;

        // Create internal Order object.
        orderptr newOrder = std::make_shared<Order>(ordertype::Limit, id, s, pr, qty);
        orderbook_.addorder(newOrder);

        // Send back an ExecutionReport as an acknowledgment.
        FIX42::ExecutionReport execReport;
        execReport.set(FIX::OrderID("EX" + clOrdID.getString()));
        execReport.set(FIX::ExecID("E" + clOrdID.getString()));
        execReport.set(FIX::ExecType('0')); // New
        execReport.set(FIX::OrdStatus('0')); // New
        execReport.set(FIX::Symbol("STOCK"));
        execReport.set(side);
        execReport.set(FIX::LeavesQty(orderQty));
        execReport.set(FIX::CumQty(0));
        FIX::Session::sendToTarget(execReport, sessionID);
    }

private:
    Orderbook& orderbook_;
    FIX::SessionID sessionID_;
};

#endif // FIXAPP_H
#pragma once
#pragma once
