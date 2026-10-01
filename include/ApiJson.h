#ifndef APIJSON_H
#define APIJSON_H

#include "ApiModels.h"

#include <optional>
#include <string>
#include <vector>

namespace ApiJson {

std::optional<ApiNewOrderRequest> parseNewOrderRequest(const std::string& json, std::string& error);
std::optional<ApiCancelOrderRequest> parseCancelOrderRequest(const std::string& json, std::string& error);
std::optional<ApiModifyOrderRequest> parseModifyOrderRequest(const std::string& json, std::string& error);

std::string serializeResponse(const ApiResponse& response);
std::string serializeOrderStatus(const ApiOrderStatus& status);
std::string serializeOrderStatuses(const std::vector<ApiOrderStatus>& statuses);
std::string serializeOrderbook(const ApiOrderbookResponse& orderbook);
std::string serializeTrades(const std::vector<ApiTradeRecord>& trades);
std::string serializeError(int status, const std::string& message);
std::string serializeHealth(bool engineRunning);

} // namespace ApiJson

#endif // APIJSON_H
