export type Side = 'BUY' | 'SELL'
export type OrderType = 'LIMIT' | 'MARKET' | 'FAK'
export type OrderStatusType = 'NEW' | 'PARTIALLY_FILLED' | 'FILLED' | 'CANCELED' | 'REPLACED' | 'REJECTED'

export interface HealthResponse {
  status: 'ok'
  engineRunning: boolean
}

export interface Level {
  price: number
  quantity: number
}

export interface OrderbookResponse {
  sequenceNumber: number
  bids: Level[]
  asks: Level[]
}

export interface TradeRecord {
  tradeId: number
  buyOrderId: number
  sellOrderId: number
  price: number
  quantity: number
  timestampNanos: number
}

export interface OrderStatus {
  orderId: number
  clientOrderId: string
  symbol: string
  side: Side
  orderType: OrderType
  price: number
  originalQuantity: number
  filledQuantity: number
  remainingQuantity: number
  status: OrderStatusType
  rejectionReason?: string
}

export interface ApiResponse {
  success: boolean
  httpStatus: number
  message: string
  sequenceNumber: number
  orderId: number
  trades: Array<{
    buyOrderId: number
    sellOrderId: number
    price: number
    quantity: number
  }>
}

export interface NewOrderRequest {
  clientOrderId: string
  symbol: string
  side: Side
  orderType: OrderType
  price?: number
  quantity: number
}

export interface CancelOrderRequest {
  orderId?: number
  clientOrderId?: string
  side?: Side
}

export interface ModifyOrderRequest {
  orderId?: number
  clientOrderId?: string
  side?: Side
  price: number
  quantity: number
}

export type WebSocketMessage =
  | ({ type: 'orderbook' } & OrderbookResponse)
  | ({ type: 'trade' } & TradeRecord)
  | {
      type: 'orderUpdate'
      sequenceNumber: number
      eventType: 'NEW' | 'CANCEL' | 'MODIFY' | 'SHUTDOWN' | 'UNKNOWN'
      orderId: number
      success: boolean
      message?: string
    }
