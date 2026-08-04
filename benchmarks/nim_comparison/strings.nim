proc record(index: int64, active: bool): string =
  result = newStringOfCap(64)
  result.add "customer-"
  result.add $index
  result.add ":active="
  result.add $active
  result.add ":balance="
  result.add $(index * 3)

echo record(0, true)
var total = 0
var index = 0'i64
var active = true
while index < 300_000:
  total += record(index, active).len
  active = not active
  index += 1
echo total
