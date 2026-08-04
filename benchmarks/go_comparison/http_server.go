package main

import (
	"html"
	"log"
	"net/http"
)

func main() {
	http.HandleFunc("/", func(response http.ResponseWriter, request *http.Request) {
		response.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = response.Write([]byte(
			"<main><h1>Aster versus Go</h1><p>Path: " +
				html.EscapeString(request.URL.Path) +
				"</p></main>",
		))
	})
	log.Fatal(http.ListenAndServe("127.0.0.1:18281", nil))
}
