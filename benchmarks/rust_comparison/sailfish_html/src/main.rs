use sailfish::TemplateOnce;

#[derive(TemplateOnce)]
#[template(path = "card.stpl")]
struct Card<'a> {
    id: i64,
    title: &'a str,
    state: &'static str,
}

fn render_card(id: i64, title: &str, active: bool) -> String {
    Card {
        id,
        title,
        state: if active { "active" } else { "paused" },
    }
    .render_once()
    .unwrap()
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
