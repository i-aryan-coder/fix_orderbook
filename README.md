# Fix_orderbook

This repository contains a FIX-based trading application that simulates order matching using an order book. The project is implemented in C++ and uses QuickFIX for FIX protocol communication.

## Features
- **Order Matching Engine**: Supports limit orders, market orders, and fill-and-kill orders.
- **FIX Protocol Support**: Implements FIX 4.4 for communication.
- **Aggregated Order Book**: Displays bids and asks after trades are executed.
- **Trade Execution Reports**: Sends execution reports as acknowledgments.

## File Structure
- `fixapp.h`: Defines the FIX application logic.
- `main.cpp`: Initializes the FIX engine and handles incoming orders.
- `orderbook.h`: Contains classes for managing orders and trades.
- `orderbook.cpp`: Implements the order matching algorithm.
- `trading_confi.cfg`: Configuration file for FIX engine settings.

## Configuration File (`trading_confi.cfg`)
Below is an example configuration file used by the application:
Below is the structure for your GitHub repository, including all files and a detailed README.md file to ensure proper readability and organization.
Repository Structure
Files Included:

    fixapp.h: Contains the implementation of the FIX application logic.

    main.cpp: Entry point for the application, initializing the FIX engine and handling orders.

    orderbook.h: Header file defining the order book and related classes.

    orderbook.cpp: Implementation of the order book logic.

    trading_confi.cfg: Configuration file for FIX engine settings.

README.md

text
# FIX Trading Application

This repository contains a FIX-based trading application that simulates order matching using an order book. The project is implemented in C++ and uses QuickFIX for FIX protocol communication.

## Features
- **Order Matching Engine**: Supports limit orders, market orders, and fill-and-kill orders.
- **FIX Protocol Support**: Implements FIX 4.4 for communication.
- **Aggregated Order Book**: Displays bids and asks after trades are executed.
- **Trade Execution Reports**: Sends execution reports as acknowledgments.

## File Structure
- `fixapp.h`: Defines the FIX application logic.
- `main.cpp`: Initializes the FIX engine and handles incoming orders.
- `orderbook.h`: Contains classes for managing orders and trades.
- `orderbook.cpp`: Implements the order matching algorithm.
- `trading_confi.cfg`: Configuration file for FIX engine settings.

## Configuration File (`trading_confi.cfg`)
Below is an example configuration file used by the application:

[DEFAULT]
ConnectionType=initiator
HeartBtInt=30
FileStorePath=store
StartTime=00:00:00
EndTime=00:00:00
UseDataDictionary=Y
SocketConnectHost=app.fixsim.com # Replace with the actual simulator host

[SESSION]
BeginString=FIX.4.4
SenderCompID=pap865601@gmail_com # Provided by FIX Sim
TargetCompID=FIXSIMDEMO # Target ID from FIX Sim
SocketConnectPort=15000 # Use correct port
DataDictionary=FIX44.xml

## Using FixSim for simulation
This project uses fixsim.com for fix simulation 


## Dependencies
- **QuickFIX**: A library for implementing the FIX protocol in C++.
- **C++17 or higher**: Required for modern C++ features used in this project.

## License
This project is licensed under the MIT License.

## Contributing
There might be some bugs in the implementation. Feel free to fork this repository, make changes, and submit pull requests!

