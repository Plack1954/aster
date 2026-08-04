package main

import "fmt"

func main() {
	var value int64 = 1
	var iteration int64
	for iteration < 20_000_000 {
		value = value + iteration + 1
		if value > 1_000_000_000 {
			value -= 1_000_000_000
		}
		iteration++
	}
	fmt.Println(value)
}
