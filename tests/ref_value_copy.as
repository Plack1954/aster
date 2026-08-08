private bool Contains(
    const ref List<string> values,
    const ref string wanted
)
{
    foreach (string value in values)
    {
        if (value == wanted) { return true; }
    }
    return false;
}

private void AppendAfterRead(ref List<string> values)
{
    int found = Contains(values, "missing") ? 1 : 0;
    Console.WriteLine(found);
    values.Add("added");
}

int main()
{
    List<string> values = new();
    AppendAfterRead(values);
    Console.WriteLine(values.Count);
    return 0;
}
