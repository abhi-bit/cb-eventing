package main

import (
	"bufio"
	"log"
	"net"
	"os"
	"os/exec"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/abhi-bit/gouch/jobpool"
)

var appLocalPortMapping map[int]string
var tcpServerWaitGroup sync.WaitGroup
var initTcpPort int = 6065

type work struct {
	conn net.Conn
	wp   *pool.WorkPool
}

func (w *work) DoWork(workRoutine int) {
	m := "hello from server"
	for len(m) != 0 {
		message := strings.ToUpper(m)
		w.conn.Write([]byte(message))
		m, _ = bufio.NewReader(w.conn).ReadString('\n')
	}

	w.conn.Close()
}

func setUpLocalTcpServer(appName string) {
	nextAvailablePort := initTcpPort
	tableLock.Lock()
	if len(appLocalPortMapping) > 0 {
		// Get next available port for listening
		for port := range appLocalPortMapping {
			if port >= nextAvailablePort {
				nextAvailablePort = port + 1
			}
		}
	}
	appLocalPortMapping[nextAvailablePort] = appName
	tableLock.Unlock()

	// Setting up local tcp server specific to the appName
	listener, err := net.Listen("tcp", "localhost:"+
		strconv.Itoa(nextAvailablePort))
	if err != nil {
		log.Fatal(err)
		listener.Close()
	}

	// Not waiting for waitgroup in initial cut as go_eventing
	// already is a long running process, but at some later
	// point it might be needed

	tcpServerWaitGroup.Add(1)
	go func(listener net.Listener) {
		defer tcpServerWaitGroup.Done()
		defer listener.Close()
		for {
			conn, err := listener.Accept()
			if err != nil {
				log.Fatal(err)
			}
			log.Printf("Post accept call:: remote addr: %s local addr: %s\n",
				conn.RemoteAddr(), conn.LocalAddr())

			w := work{
				conn: conn,
				wp:   workPool,
			}

			if err := workPool.PostWork(appName, &w); err != nil {
				log.Fatal(err)
			}
		}
	}(listener)

	time.Sleep(1 * time.Second)

	go func() {
		cwd, _ := os.Getwd()
		log.Printf("Os cwd: %s\n", cwd)
		cmd := exec.Command("./client", strconv.Itoa(nextAvailablePort))
		out, err := cmd.CombinedOutput()
		if err != nil {
			log.Fatal(err)
		}
		log.Printf("out: %s", out)
	}()
}
