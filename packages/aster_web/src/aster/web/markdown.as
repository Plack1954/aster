namespace Aster.Web.Markdown;

using Aster.Html;
using System.Text;

private void MarkdownAppendEscaped(
    ref StringBuilder output,
    const ref string value
)
{
    for (nuint index = 0; index < value.Length; index++)
    {
        byte current = StringByteAt(value, index);
        if (current == 38) { output.Append("&amp;"); }
        else if (current == 60) { output.Append("&lt;"); }
        else if (current == 62) { output.Append("&gt;"); }
        else if (current == 34) { output.Append("&quot;"); }
        else { output.AppendByte(current); }
    }
}

private void MarkdownAppendInline(
    ref StringBuilder output,
    const ref string value
)
{
    nuint cursor = 0;
    nuint segment = 0;
    bool strong = false;
    while (cursor + 1 < value.Length)
    {
        if (StringByteAt(value, cursor) == 42 &&
            StringByteAt(value, cursor + 1) == 42)
        {
            MarkdownAppendEscaped(output, StringSlice(value, segment, cursor));
            if (strong) { output.Append("</strong>"); }
            else { output.Append("<strong>"); }
            strong = !strong;
            cursor += 2;
            segment = cursor;
        }
        else
        {
            cursor += 1;
        }
    }
    MarkdownAppendEscaped(
        output, StringSlice(value, segment, value.Length)
    );
    if (strong) { output.Append("</strong>"); }
}

private bool MarkdownOrderedItem(const ref string line)
{
    nuint cursor = 0;
    while (cursor < line.Length &&
           StringByteAt(line, cursor) >= 48 &&
           StringByteAt(line, cursor) <= 57)
    {
        cursor += 1;
    }
    return cursor > 0 && cursor + 1 < line.Length &&
        StringByteAt(line, cursor) == 46 &&
        StringByteAt(line, cursor + 1) == 32;
}

private nuint MarkdownOrderedTextStart(const ref string line)
{
    nuint cursor = 0;
    while (cursor < line.Length &&
           StringByteAt(line, cursor) >= 48 &&
           StringByteAt(line, cursor) <= 57)
    {
        cursor += 1;
    }
    return cursor + 2;
}

private nuint MarkdownHeadingLevel(const ref string line)
{
    nuint level = 0;
    while (level < line.Length && level < 6 &&
           StringByteAt(line, level) == 35)
    {
        level += 1;
    }
    if (level == 0 || level >= line.Length ||
        StringByteAt(line, level) != 32)
    {
        return 0;
    }
    return level;
}

private string MarkdownHeadingOpen(nuint level)
{
    if (level == 1) { return "<h1>"; }
    if (level == 2) { return "<h2>"; }
    if (level == 3) { return "<h3>"; }
    if (level == 4) { return "<h4>"; }
    if (level == 5) { return "<h5>"; }
    return "<h6>";
}

private string MarkdownHeadingClose(nuint level)
{
    if (level == 1) { return "</h1>"; }
    if (level == 2) { return "</h2>"; }
    if (level == 3) { return "</h3>"; }
    if (level == 4) { return "</h4>"; }
    if (level == 5) { return "</h5>"; }
    return "</h6>";
}

public Html Markdown(string source)
{
    StringBuilder output = new();
    bool listOpen = false;
    nuint cursor = 0;
    while (cursor < source.Length)
    {
        nuint end = cursor;
        while (end < source.Length && StringByteAt(source, end) != 10)
        {
            end += 1;
        }
        string line = StringSlice(source, cursor, end);
        nuint headingLevel = MarkdownHeadingLevel(line);
        if (headingLevel > 0)
        {
            if (listOpen)
            {
                output.Append("</ol>");
                listOpen = false;
            }
            output.Append(MarkdownHeadingOpen(headingLevel));
            MarkdownAppendInline(output, StringSlice(
                line, headingLevel + 1, line.Length
            ));
            output.Append(MarkdownHeadingClose(headingLevel));
        }
        else if (MarkdownOrderedItem(line))
        {
            if (!listOpen)
            {
                output.Append("<ol>");
                listOpen = true;
            }
            output.Append("<li>");
            MarkdownAppendInline(output, StringSlice(
                line, MarkdownOrderedTextStart(line), line.Length
            ));
            output.Append("</li>");
        }
        else
        {
            if (listOpen)
            {
                output.Append("</ol>");
                listOpen = false;
            }
            if (line.Length > 0)
            {
                output.Append("<p>");
                MarkdownAppendInline(output, line);
                output.Append("</p>");
            }
        }
        cursor = end < source.Length ? end + 1 : end;
    }
    if (listOpen) { output.Append("</ol>"); }
    return Html.UnsafeRaw(output.ToString());
}
