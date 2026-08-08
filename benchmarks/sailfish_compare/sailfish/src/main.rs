use sailfish::TemplateSimple;

const ITERATIONS: usize = 250_000;
const WARMUP: usize = 1_000;
const VALUE: &str = "A&B <tag> / unicode: Melbourne";

#[derive(TemplateSimple)]
#[template(path = "escaped.stpl")]
struct Escaped<'a> {
    value: &'a str,
    iteration: usize,
}

#[derive(TemplateSimple)]
#[template(path = "raw.stpl")]
struct Raw<'a> {
    value: &'a str,
    iteration: usize,
}

fn main() {
    let mode = std::env::args().nth(1).expect("expected escaped or raw");
    let mut bytes = 0usize;
    for iteration in 0..WARMUP {
        let output = match mode.as_str() {
            "escaped" => Escaped { value: VALUE, iteration }.render_once().unwrap(),
            "raw" => Raw { value: VALUE, iteration }.render_once().unwrap(),
            _ => panic!("expected escaped or raw"),
        };
        bytes = bytes.wrapping_add(output.len());
    }
    bytes = 0;
    for iteration in 0..ITERATIONS {
        let output = match mode.as_str() {
            "escaped" => Escaped { value: VALUE, iteration }.render_once().unwrap(),
            "raw" => Raw { value: VALUE, iteration }.render_once().unwrap(),
            _ => panic!("expected escaped or raw"),
        };
        bytes = bytes.wrapping_add(output.len());
    }
    println!("{bytes}");
}
