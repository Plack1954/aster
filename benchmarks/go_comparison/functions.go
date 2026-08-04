package main

import "fmt"

func mix(value, iteration int64) int64 {
	next := value + iteration + 1
	if next > 1_000_000_000 {
		return next - 1_000_000_000
	}
	return next
}

func main() {
	var value int64 = 1
	var iteration int64
	for iteration < 10_000_000 {
		value = mix(value, iteration)
		iteration++
	}
	fmt.Println(value)
}
