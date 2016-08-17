package eventing_test

import (
	"github.com/abhi-bit/eventing/worker"
	"testing"
)

type loadtestentry struct {
	appname string
	source  string
	result  string
}

type sendupdateentry struct {
	value      string
	metadata   string
	contenType string
	result     string
}

var loadTests = []loadtestentry{
	{"app1", "function OnUpdate(doc, meta) { log(meta.id); }", ""},
	{"app2", "function OnDelete(meta) { log(meta.id); }", ""},
	{"app3", "function OnDelete(meta) { log(meta.id); ",
		"undefined:29\n}\n^\nSyntaxError: Unexpected end of input\n"},
}

func TestHandleLoad(t *testing.T) {
	for _, entry := range loadTests {
		handle := worker.New(entry.appname)
		err := handle.Load(entry.appname, entry.source)
		handle.Dispose()

		if err.Error() != entry.result {
			t.Error(
				"For", entry.source,
				"expected", entry.result,
				"got", err,
			)
		}
	}
}

var sendUpdateTests = []sendupdateentry{
	{"{\"credit_card_count\":2,\"credit_score\":430,\"total_credit_limit\":46977,\"ssn\":\"335_12_2551\",\"credit_limit_used\":9395,\"missed_emi_payments\":3}",
		"{\"key\":\"ijk335_12_2551\",\"type\":\"json\",\"cas\":\"270df63d0000\",\"expiry\":\"0\"}",
		"json", ""},
}

func TestHandleUpdate(t *testing.T) {
	handle := worker.New("app1")
	err := handle.Load("app1", "function OnUpdate(doc, meta) { log(meta); }\n function OnDelete() {}\n function OnHTTPGet(req, res) {}\n function OnHTTPPost(req, res) {}")

	if err != nil {
		for _, entry := range sendUpdateTests {
			updateErr := handle.SendUpdate(entry.value,
				entry.metadata,
				entry.contenType)
			if updateErr != nil {
				t.Error(
					"Error received",
					updateErr.Error(),
				)
			}
		}
	}
	handle.Dispose()
}
