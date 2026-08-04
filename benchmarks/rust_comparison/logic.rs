fn main() {
    let mut value: i64 = 1;
    let mut iteration: i64 = 0;
    while iteration < 20_000_000 {
        value = value + iteration + 1;
        if value > 1_000_000_000 {
            value -= 1_000_000_000;
        }
        iteration += 1;
    }
    println!("{value}");
}
