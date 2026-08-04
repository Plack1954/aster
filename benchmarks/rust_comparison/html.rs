use std::fmt::Write;

fn push_escaped_text(output: &mut String, value: &str) {
    for character in value.chars() {
        match character {
            '&' => output.push_str("&amp;"),
            '<' => output.push_str("&lt;"),
            '>' => output.push_str("&gt;"),
            _ => output.push(character),
        }
    }
}

fn render_card(id: i64, title: &str, active: bool) -> String {
    let state = if active { "active" } else { "paused" };
    let mut output = String::with_capacity(128);
    write!(&mut output, "<article class=\"card\" data-id=\"{id}\"><h2>").unwrap();
    push_escaped_text(&mut output, title);
    write!(
        &mut output,
        "</h2><p>Customer #{id} is {state}.</p></article>"
    )
    .unwrap();
    output
}

fn main() {
    println!("{}", render_card(0, "A&B <Aster>", true));

    let mut total: usize = 0;
    let mut index: i64 = 0;
    let mut active = true;
    while index < 200_000 {
        total += render_card(index, "A&B <Aster>", active).len();
        active = !active;
        index += 1;
    }
    println!("{total}");
}
