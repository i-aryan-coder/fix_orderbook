# Trading Engine 2.0 Frontend

React + TypeScript + Vite trading terminal for the C++ matching-engine backend.

## Runtime model

- REST is used for commands and initial state loading.
- WebSocket is used for live `orderUpdate`, `orderbook`, and `trade` events.
- The frontend never performs matching, FIFO priority, or execution-state inference; backend state is authoritative.

## Configuration

Create a local `.env` from `.env.example` when overriding defaults.

```text
VITE_API_BASE_URL=http://127.0.0.1:18080
VITE_WS_URL=ws://127.0.0.1:18080/ws
```

Production Render values:

```text
VITE_API_BASE_URL=https://tradex-i7n2.onrender.com
VITE_WS_URL=wss://tradex-i7n2.onrender.com/ws
```

## Development

```bash
npm ci
npm run dev
```

## Validation

```bash
npm run build
npm run lint
```

## Deployment

Render Static Site configuration:

```text
Root Directory: frontend
Build Command: npm ci && npm run build
Publish Directory: dist
```

Live site: https://tradepro-ggba.onrender.com
