proc mix(value, iteration: int64): int64 =
  let next = value + iteration + 1
  if next > 1_000_000_000:
    return next - 1_000_000_000
  next

var value = 1'i64
var iteration = 0'i64
while iteration < 10_000_000:
  value = mix(value, iteration)
  iteration += 1
echo value
