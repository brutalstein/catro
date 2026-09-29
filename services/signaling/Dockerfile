FROM golang:1.27.1-alpine AS build
WORKDIR /src
COPY go.mod go.sum ./
RUN go mod download
COPY . .
RUN CGO_ENABLED=0 go build -trimpath -ldflags="-s -w" -o /out/catro-signaling .

FROM gcr.io/distroless/static-debian12:nonroot
COPY --from=build /out/catro-signaling /catro-signaling
USER nonroot:nonroot
EXPOSE 8443
ENTRYPOINT ["/catro-signaling"]
