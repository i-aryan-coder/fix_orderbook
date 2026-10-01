import type { OrderStatus, TradeRecord } from '../types/api'

interface ExecutionPanelProps {
  selectedOrder: OrderStatus | null
  trades: TradeRecord[]
}

export function ExecutionPanel({ selectedOrder, trades }: ExecutionPanelProps) {
  const relatedTrades = selectedOrder
    ? trades.filter((trade) => trade.buyOrderId === selectedOrder.orderId || trade.sellOrderId === selectedOrder.orderId)
    : []
  const fields = selectedOrder
    ? [
        ['Order ID', selectedOrder.orderId],
        ['Status', selectedOrder.status],
        ['ClOrdID', selectedOrder.clientOrderId],
        ['Filled quantity', selectedOrder.filledQuantity],
        ['Remaining qty', selectedOrder.remainingQuantity],
        ['Last execution Px', relatedTrades[0]?.price ?? '-'],
      ]
    : []

  return (
    <section className="panel execution-panel">
      <div className="panel-header execution-header">
        <h2>Execution information</h2>
        <span className="confirmed-badge">Confirmed</span>
      </div>
      {selectedOrder ? (
        <div className="execution-fields">
          {fields.map(([label, value]) => (
            <div className="execution-field" key={label}>
              <span>{label}</span>
              <strong>{value}</strong>
            </div>
          ))}
        </div>
      ) : (
        <div className="empty-state">Select an order to inspect backend-confirmed execution state</div>
      )}
    </section>
  )
}
