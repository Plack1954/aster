package main

import (
	"fmt"
	"html"
	"strconv"
	"strings"
)

func renderCard(id int64, title string, active bool) string {
	state := "paused"
	if active {
		state = "active"
	}
	var output strings.Builder
	output.Grow(128)
	output.WriteString(`<article class="card" data-id="`)
	output.WriteString(strconv.FormatInt(id, 10))
	output.WriteString(`"><h2>`)
	output.WriteString(html.EscapeString(title))
	output.WriteString(`</h2><p>Customer #`)
	output.WriteString(strconv.FormatInt(id, 10))
	output.WriteString(" is ")
	output.WriteString(state)
	output.WriteString(".</p></article>")
	return output.String()
}

func main() {
	fmt.Println(renderCard(0, "A&B <Aster>", true))

	var total int
	var index int64
	active := true
	for index < 200_000 {
		total += len(renderCard(index, "A&B <Aster>", active))
		active = !active
		index++
	}
	fmt.Println(total)
}
