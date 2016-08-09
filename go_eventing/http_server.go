package main

import (
	"errors"
	"log"
	"net"
	"time"
)

type networkAddr struct {
	networkName string
	addrStr     string
}

// HTTPServer interface that would allow server shutdown
type HTTPServer struct {
	Listener *net.TCPListener
	netAddr  *networkAddr
}

// Network function
func (netAddr *networkAddr) Network() string {
	return netAddr.networkName
}

// String function
func (netAddr *networkAddr) String() string {
	return netAddr.addrStr
}

func createHTTPServer(listener net.Listener) *HTTPServer {
	tcpListener, _ := listener.(*net.TCPListener)

	server := &HTTPServer{
		Listener: tcpListener,
	}

	return server
}

// Addr function
func (server *HTTPServer) Addr() net.Addr {
	return server.netAddr
}

// Accept function
func (server *HTTPServer) Accept() (c net.Conn, err error) {
	log.SetFlags(log.Lshortfile | log.LstdFlags)
	for {
		server.Listener.SetDeadline(
			time.Now().Add(100 * time.Millisecond))

		c, err := server.Listener.Accept()
		if err != nil {
			netErr, ok := err.(net.Error)

			if ok && netErr.Timeout() && netErr.Temporary() {
				continue
			}
		}
		return c, err
	}
}

// Close function
func (server *HTTPServer) Close() error {
	return errors.New("Closed http server")
}
