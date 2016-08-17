package eventing_test

import (
	"github.com/abhi-bit/eventing/worker"
	"testing"
)

type sendupdateentry struct {
	value      string
	metadata   string
	contenType string
	result     string
}

var entry = sendupdateentry{
	"{\"credit_card_count\":2,\"credit_score\":430,\"total_credit_limit\":46977,\"ssn\":\"335_12_2551\",\"credit_limit_used\":9395,\"missed_emi_payments\":3}",
	"{\"key\":\"ijk335_12_2551\",\"type\":\"json\",\"cas\":\"270df63d0000\",\"expiry\":\"0\"}",
	"json", ""}

func BenchmarkCGOCall(b *testing.B) {
	handle := worker.New("app1")
	handle.Load("app1", "function OnUpdate(doc, meta) { }\n function OnDelete() {}\n function OnHTTPGet(req, res) {}\n function OnHTTPPost(req, res) {}")

	for n := 0; n < b.N; n++ {
		updateErr := handle.SendUpdate(entry.value,
			entry.metadata,
			entry.contenType)
		if updateErr != nil {
			b.Error(updateErr)
		}
	}
}

func BenchmarkBucketSet(b *testing.B) {
	handle := worker.New("app1")
	handle.Load("app1", "function OnUpdate(doc, meta) { credit_bucket[meta.key] = doc; }\n function OnDelete() {}\n function OnHTTPGet(req, res) {}\n function OnHTTPPost(req, res) {}")

	for n := 0; n < b.N; n++ {
		handle.SendUpdate(entry.value,
			entry.metadata,
			entry.contenType)
	}
}

func BenchmarkBucketGet(b *testing.B) {
	handle := worker.New("app1")
	handle.Load("app1", "function OnUpdate(doc, meta) { var obj = credit_bucket[meta.key]; }\n function OnDelete() {}\n function OnHTTPGet(req, res) {}\n function OnHTTPPost(req, res) {}")

	for n := 0; n < b.N; n++ {
		handle.SendUpdate(entry.value,
			entry.metadata,
			entry.contenType)
	}
}

func BenchmarkEnqueue(b *testing.B) {
	handle := worker.New("app1")
	handle.Load("app1", "function OnUpdate(doc, meta) { enqueue(order_queue, meta.key); }\n function OnDelete() {}\n function OnHTTPGet(req, res) {}\n function OnHTTPPost(req, res) {}")

	for n := 0; n < b.N; n++ {
		handle.SendUpdate(entry.value,
			entry.metadata,
			entry.contenType)
	}
}
