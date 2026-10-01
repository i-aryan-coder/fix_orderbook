import type { OrderStatus } from '../types/api'

interface OrdersTableProps {
  orders: OrderStatus[]
  selectedOrderId: number | null
  onSelect: (order: OrderStatus) => void
}

export function OrdersTable({ orders, selectedOrderId, onSelect }: OrdersTableProps) {
  return (
    <section className="panel orders-panel">
      <div className="panel-header">
        <h2>Orders</h2>
        <span>Backend state</span>
      </div>
      <table>
        <thead>
          <tr>
            <th>Order ID</th>
            <th>ClOrdID</th>
            <th>Side</th>
            <th>Price</th>
            <th>Filled</th>
            <th>Remain</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody>
          {orders.map((order) => (
            <tr
              className={order.orderId === selectedOrderId ? 'selected-row' : ''}
              key={order.orderId}
              onClick={() => onSelect(order)}
            >
              <td>{order.orderId}</td>
              <td>{order.clientOrderId}</td>
              <td className={order.side === 'BUY' ? 'buy-text' : 'sell-text'}>{order.side}</td>
              <td>{order.orderType === 'MARKET' ? 'MKT' : order.price}</td>
              <td>{order.filledQuantity}</td>
              <td>{order.remainingQuantity}</td>
              <td>{order.status}</td>
            </tr>
          ))}
        </tbody>
      </table>
      {orders.length === 0 && <div className="empty-state">No orders yet</div>}
    </section>
  )
}
