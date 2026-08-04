fn mix(value: i64, iteration: i64) -> i64 {
    let next = value + iteration + 1;
    if next > 1_000_000_000 {
        return next - 1_000_000_000;
    }
    next
}

fn main() {
    let mut value: i64 = 1;
    let mut iteration: i64 = 0;
    while iteration < 10_000_000 {
        value = mix(value, iteration);
        iteration += 1;
    }
    println!("{value}");
}
