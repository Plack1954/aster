proc addEscapedText(output: var string, value: string) =
  for character in value:
    case character
    of '&': output.add "&amp;"
    of '<': output.add "&lt;"
    of '>': output.add "&gt;"
    else: output.add character

proc renderCard(id: int64, title: string, active: bool): string =
  let state = if active: "active" else: "paused"
  result = newStringOfCap(128)
  result.add "<article class=\"card\" data-id=\""
  result.add $id
  result.add "\"><h2>"
  result.addEscapedText title
  result.add "</h2><p>Customer #"
  result.add $id
  result.add " is "
  result.add state
  result.add ".</p></article>"

echo renderCard(0, "A&B <Aster>", true)
var total = 0
var index = 0'i64
var active = true
while index < 200_000:
  total += renderCard(index, "A&B <Aster>", active).len
  active = not active
  index += 1
echo total
