package main

import (
	"fmt"
	"strconv"
	"strings"
)

func record(index int64, active bool) string {
	var output strings.Builder
	output.Grow(64)
	output.WriteString("customer-")
	output.WriteString(strconv.FormatInt(index, 10))
	output.WriteString(":active=")
	output.WriteString(strconv.FormatBool(active))
	output.WriteString(":balance=")
	output.WriteString(strconv.FormatInt(index*3, 10))
	return output.String()
}

func main() {
	fmt.Println(record(0, true))

	var total int
	var index int64
	active := true
	for index < 300_000 {
		total += len(record(index, active))
		active = !active
		index++
	}
	fmt.Println(total)
}
