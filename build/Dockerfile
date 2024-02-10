FROM alpine:3.19

WORKDIR /app

RUN apk update && apk add make build-base

COPY Makefile Makefile
COPY util util
COPY src src

RUN make nat && \
    apk del make build-base && \
    rm -rf src util Makefile build

CMD ["./nat"]