FROM alpine:3.19

WORKDIR /app
ENV NAT_BASE_DIR=/app

RUN apk update && apk add make build-base python3

COPY Makefile Makefile
COPY src src
COPY build build

RUN make install && \
    cp build/nat /bin/nat && \
    apk del make build-base python3 && \
    rm -rf src util Makefile build

CMD ["./nat"]