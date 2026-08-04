use std::fmt::Write;

fn record(index: i64, active: bool) -> String {
    let mut output = String::with_capacity(64);
    write!(
        &mut output,
        "customer-{index}:active={active}:balance={}",
        index * 3
    )
    .unwrap();
    output
}

fn main() {
    println!("{}", record(0, true));

    let mut total: usize = 0;
    let mut index: i64 = 0;
    let mut active = true;
    while index < 300_000 {
        total += record(index, active).len();
        active = !active;
        index += 1;
    }
    println!("{total}");
}
