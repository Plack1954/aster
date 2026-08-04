var value = 1'i64
var iteration = 0'i64
while iteration < 20_000_000:
  value = value + iteration + 1
  if value > 1_000_000_000:
    value -= 1_000_000_000
  iteration += 1
echo value
