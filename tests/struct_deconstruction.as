private struct ResponseParts {
    int status;
    string body;
    List<string> headers;
}

private ResponseParts MakeResponse() {
    List<string> headers = new();
    headers.Add("Content-Type: text/html");
    return new() {
        status = 201,
        body = "created",
        headers = headers,
    };
}

int main() {
    ResponseParts response = MakeResponse();
    ResponseParts responseCopy = copy(response);
    (int status, string body, List<string> headers) = responseCopy;
    Console.WriteLine(status);
    Console.WriteLine(body);
    Console.WriteLine(headers.Count);
    Console.WriteLine(response.status);
    return 0;
}
