use std::fmt::Write as FmtWrite;
use std::io::{Read, Write as IoWrite};
use std::net::{TcpListener, TcpStream};

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

fn serve_connection(mut stream: TcpStream) {
    let mut request = [0_u8; 16_384];
    let count = match stream.read(&mut request) {
        Ok(0) | Err(_) => return,
        Ok(count) => count,
    };
    let request_text = String::from_utf8_lossy(&request[..count]);
    let path = request_text
        .lines()
        .next()
        .and_then(|line| line.split_whitespace().nth(1))
        .unwrap_or("/");

    let mut body = String::with_capacity(96);
    body.push_str("<main><h1>Aster versus Rust</h1><p>Path: ");
    push_escaped_text(&mut body, path);
    body.push_str("</p></main>");

    let mut response = String::with_capacity(body.len() + 128);
    write!(
        &mut response,
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
        body.len()
    )
    .unwrap();
    response.push_str(&body);
    let _ = stream.write_all(response.as_bytes());
}

fn main() {
    let listener = TcpListener::bind("127.0.0.1:18381").unwrap();
    for connection in listener.incoming() {
        if let Ok(stream) = connection {
            serve_connection(stream);
        }
    }
}
