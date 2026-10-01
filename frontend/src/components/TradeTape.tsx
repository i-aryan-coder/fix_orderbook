import type { TradeRecord } from '../types/api'

interface TradeTapeProps {
  trades: TradeRecord[]
}

function formatTime(timestampNanos: number) {
  if (!timestampNanos) {
    return '-'
  }
  const millis = Math.floor(timestampNanos / 1_000_000)
  return new Date(millis).toLocaleTimeString()
}

export function TradeTape({ trades }: TradeTapeProps) {
  return (
    <section className="panel">
      <div className="panel-header">
        <h2>Trade tape</h2>
        <span>WebSocket stream</span>
      </div>
      <table>
        <thead>
          <tr>
            <th>Time</th>
            <th>Price</th>
            <th>Qty</th>
          </tr>
        </thead>
        <tbody>
          {trades.map((trade) => (
            <tr key={trade.tradeId}>
              <td>{formatTime(trade.timestampNanos)}</td>
              <td>{trade.price}</td>
              <td>{trade.quantity}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {trades.length === 0 && <div className="empty-state">No trades yet</div>}
    </section>
  )
}
