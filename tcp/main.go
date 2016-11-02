package main

import (
	"bufio"
	"fmt"
	"log"
	"net"
	"os/exec"
	"runtime"
	"strings"
	"sync"
	"time"

	"github.com/abhi-bit/gouch/jobpool"
)

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

func main() {
	workPool := pool.New(runtime.NumCPU(), 100)
	var wg sync.WaitGroup

	ln, err := net.Listen("tcp", "127.0.0.1:3491")
	if err != nil {
		log.Fatal(err)
		ln.Close()
	}

	wg.Add(2)

	go func(workPool *pool.WorkPool, ln net.Listener) {
		defer wg.Done()
		for {
			c, err := ln.Accept()
			fmt.Printf("Post accept call:: remote addr: %s local addr: %s\n",
				c.RemoteAddr(), c.LocalAddr())
			if err != nil {
				log.Fatal(err)
			}

			w := work{
				conn: c,
				wp:   workPool,
			}

			if err := workPool.PostWork("routine", &w); err != nil {
				log.Println(err)
			}
		}
	}(workPool, ln)

	time.Sleep(1 * time.Second)

	go func() {
		defer wg.Done()
		cmd := exec.Command("./client", "3491")
		out, err := cmd.CombinedOutput()
		if err != nil {
			log.Fatal("Cmd")
		}
		log.Printf("out: %s", out)
	}()

	wg.Wait()
	ln.Close()
}
