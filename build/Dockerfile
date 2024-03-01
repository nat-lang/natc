FROM alpine:3.19

WORKDIR /app

RUN apk update && apk add make build-base

COPY Makefile Makefile
COPY src src
COPY build build

RUN make nat && \
    cp build/nat /bin/nat && \
    apk del make build-base && \
    rm -rf src util Makefile build

CMD ["./nat"]