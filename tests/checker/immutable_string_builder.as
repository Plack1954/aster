private void AppendText(StringBuilder builder) {
    builder.Append("no");
}

int main() {
    StringBuilder builder = new();
    AppendText(builder);
    return 0;
}
