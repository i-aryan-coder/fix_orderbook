import { useCallback, useEffect, useMemo, useState } from 'react'
import { getHealth, getOrderbook, getOrders, getTrades, submitOrder } from './api/client'
import { ConnectionStatus } from './components/ConnectionStatus'
import { ExecutionPanel } from './components/ExecutionPanel'
import { OrderBook } from './components/OrderBook'
import { OrderEntry } from './components/OrderEntry'
import { OrdersTable } from './components/OrdersTable'
import { TradeTape } from './components/TradeTape'
import { useTradingWebSocket } from './hooks/useTradingWebSocket'
import type { ApiResponse, NewOrderRequest, OrderStatus, OrderbookResponse, TradeRecord, WebSocketMessage } from './types/api'
import './App.css'

function App() {
  const [engineRunning, setEngineRunning] = useState<boolean | null>(null)
  const [orderbook, setOrderbook] = useState<OrderbookResponse | null>(null)
  const [trades, setTrades] = useState<TradeRecord[]>([])
  const [orders, setOrders] = useState<OrderStatus[]>([])
  const [selectedOrderId, setSelectedOrderId] = useState<number | null>(null)
  const [error, setError] = useState<string | null>(null)

  const refreshState = useCallback(async () => {
    try {
      const [health, freshOrderbook, freshTrades, freshOrders] = await Promise.all([
        getHealth(),
        getOrderbook(),
        getTrades(),
        getOrders(),
      ])
      setEngineRunning(health.engineRunning)
      setOrderbook(freshOrderbook)
      setTrades([...freshTrades].reverse())
      setOrders(freshOrders)
      setError(null)
    } catch (refreshError) {
      setError(refreshError instanceof Error ? refreshError.message : 'Unable to refresh backend state')
    }
  }, [])

  useEffect(() => {
    refreshState()
  }, [refreshState])

  const handleWsMessage = useCallback((message: WebSocketMessage) => {
    if (message.type === 'orderbook') {
      setOrderbook({ sequenceNumber: message.sequenceNumber, bids: message.bids, asks: message.asks })
      return
    }
    if (message.type === 'trade') {
      setTrades((current) => [message, ...current.filter((trade) => trade.tradeId !== message.tradeId)].slice(0, 50))
      return
    }
    if (message.type === 'orderUpdate') {
      refreshState()
    }
  }, [refreshState])

  const { connectionState, lastSequenceNumber, sequenceWarning } = useTradingWebSocket({
    onMessage: handleWsMessage,
    onSequenceGap: refreshState,
  })

  const selectedOrder = useMemo(
    () => orders.find((order) => order.orderId === selectedOrderId) ?? null,
    [orders, selectedOrderId],
  )

  const handleSubmitOrder = async (order: NewOrderRequest): Promise<ApiResponse> => {
    const response = await submitOrder(order)
    setSelectedOrderId(response.orderId)
    await refreshState()
    return response
  }

  return (
    <main className="terminal-shell">
      <header className="terminal-header">
        <div>
          <p className="eyebrow">Trading Engine 2.0</p>
          <h1>Live trading terminal</h1>
        </div>
        <ConnectionStatus
          engineRunning={engineRunning}
          connectionState={connectionState}
          lastSequenceNumber={lastSequenceNumber}
          sequenceWarning={sequenceWarning}
          error={error}
        />
      </header>

      <section className="dashboard-grid">
        <OrderEntry onSubmit={handleSubmitOrder} />
        <OrderBook orderbook={orderbook} />
        <TradeTape trades={trades} />
        <OrdersTable orders={orders} selectedOrderId={selectedOrderId} onSelect={(order) => setSelectedOrderId(order.orderId)} />
        <ExecutionPanel selectedOrder={selectedOrder} trades={trades} />
      </section>
    </main>
  )
}

export default App
