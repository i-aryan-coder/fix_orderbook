FROM debian:bookworm-slim AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends g++ make ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY Makefile ./
COPY include ./include
COPY src ./src
COPY tests ./tests

RUN make api-server API_SERVER_BIN=api_server CXXFLAGS="-std=c++17 -Wall -Wextra -Wpedantic -O2 -Iinclude"

FROM debian:bookworm-slim AS runtime

RUN useradd --system --uid 10001 --create-home appuser
WORKDIR /app

COPY --from=build /app/api_server ./api_server

ENV API_HOST=0.0.0.0 \
    CORS_ALLOWED_ORIGIN=https://tradepro-ggba.onrender.com

EXPOSE 8080
USER appuser

CMD ["./api_server"]
