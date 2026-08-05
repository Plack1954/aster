using System.Net;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;
using Microsoft.AspNetCore.Builder;
using Microsoft.AspNetCore.Hosting;
using Microsoft.AspNetCore.Http;

if (args.Length != 1)
{
    Console.Error.WriteLine("usage: AsterCSharpBenchmarks WORKLOAD");
    return 2;
}

if (args[0] == "http")
{
    WebApplicationBuilder builder = WebApplication.CreateSlimBuilder();
    builder.WebHost.UseUrls("http://127.0.0.1:18481");
    WebApplication app = builder.Build();
    app.MapGet("/benchmark", (HttpRequest request) =>
    {
        string path = request.Path + request.QueryString;
        string body =
            "<main><h1>Aster versus C#</h1><p>Path: " +
            WebUtility.HtmlEncode(path) + "</p></main>";
        return Results.Content(body, "text/html; charset=utf-8");
    });
    app.Run();
    return 0;
}

long result = args[0] switch
{
    "arithmetic" => Arithmetic(),
    "function_calls" => FunctionCalls(),
    "branches" => Branches(),
    "list_growth" => ListGrowth(),
    "list_scan" => ListScan(),
    "dictionary" => DictionaryWorkload(),
    "hash_set" => HashSetWorkload(),
    "queue" => QueueWorkload(),
    "owned_strings" => OwnedStrings(),
    "string_builder" => StringBuilderWorkload(),
    "text_search" => TextSearch(),
    "json_parse" => JsonParse(),
    "exceptions" => Exceptions(),
    "html_render" => HtmlRender(),
    _ => throw new ArgumentException($"unknown workload: {args[0]}")
};

Console.WriteLine(result);
return 0;

static long Arithmetic()
{
    long value = 1;
    for (long i = 0; i < 50_000_000; i++)
    {
        value += (i % 97) + 1;
        if (value > 1_000_000_000) value -= 1_000_000_000;
    }
    return value;
}

[MethodImpl(MethodImplOptions.AggressiveInlining)]
static long Mix(long value, long i)
{
    long next = value + i + 1;
    return next > 1_000_000_000 ? next - 1_000_000_000 : next;
}

static long FunctionCalls()
{
    long value = 1;
    for (long i = 0; i < 20_000_000; i++) value = Mix(value, i);
    return value;
}

static long Branches()
{
    long total = 0;
    for (long i = 0; i < 50_000_000; i++)
    {
        long kind = i % 8;
        if (kind == 0) total += 3;
        else if (kind == 1) total -= 2;
        else if (kind == 2) total += i % 17;
        else if (kind == 3) total ^= kind;
        else if (kind == 4) total += 11;
        else if (kind == 5) total -= i % 5;
        else if (kind == 6) total += 7;
        else total -= 1;
    }
    return total;
}

static long ListGrowth()
{
    List<int> values = [];
    for (int i = 0; i < 2_000_000; i++) values.Add(i % 1024);
    long total = 0;
    for (int i = 0; i < values.Count; i++) total += values[i];
    return total;
}

static long ListScan()
{
    List<int> values = new(2_000_000);
    for (int i = 0; i < 2_000_000; i++) values.Add(i % 1024);
    long total = 0;
    for (int pass = 0; pass < 8; pass++)
        for (int i = 0; i < values.Count; i++) total += values[i];
    return total;
}

static long DictionaryWorkload()
{
    Dictionary<int, int> values = new(500_000);
    for (int i = 0; i < 500_000; i++) values[i] = (i * 17) % 1_000_003;
    long total = 0;
    for (int i = 0; i < 500_000; i++) total += values[i];
    for (int i = 0; i < 500_000; i += 3) values.Remove(i);
    return total + values.Count;
}

static long HashSetWorkload()
{
    HashSet<int> values = new(1_000_000);
    for (int i = 0; i < 1_000_000; i++) values.Add(i);
    long found = 0;
    for (int i = 0; i < 2_000_000; i++)
        if (values.Contains(i % 1_250_000)) found++;
    for (int i = 0; i < 1_000_000; i += 4) values.Remove(i);
    return found + values.Count;
}

static long QueueWorkload()
{
    Queue<int> values = new(2_000_000);
    for (int i = 0; i < 2_000_000; i++) values.Enqueue(i % 1024);
    long total = 0;
    while (values.Count != 0) total += values.Dequeue();
    return total;
}

static long OwnedStrings()
{
    long total = 0;
    bool active = true;
    for (long i = 0; i < 500_000; i++)
    {
        string value = $"customer-{i}:active={active}:balance={i * 3}";
        total += value.Length;
        active = !active;
    }
    return total;
}

static long StringBuilderWorkload()
{
    StringBuilder builder = new();
    for (int i = 0; i < 1_000_000; i++) builder.Append(i % 10);
    return builder.ToString().Length;
}

static long TextSearch()
{
    const string text = "Aster makes native web development fast and predictable";
    long matches = 0;
    for (int i = 0; i < 5_000_000; i++)
    {
        if (text.StartsWith("Aster", StringComparison.Ordinal)) matches++;
        if (text.EndsWith("predictable", StringComparison.Ordinal)) matches++;
        if (text.Contains("native web", StringComparison.Ordinal)) matches++;
        matches += text.IndexOf("development", StringComparison.Ordinal);
    }
    return matches;
}

static long JsonParse()
{
    const string source =
        "{\"name\":\"Aster\",\"count\":42,\"ready\":true,\"items\":[1,2,3,4,5]}";
    long total = 0;
    for (int i = 0; i < 50_000; i++)
    {
        using JsonDocument document = JsonDocument.Parse(source);
        JsonElement root = document.RootElement;
        total += root.GetProperty("count").GetInt32();
        total += root.GetProperty("items").GetArrayLength();
        if (root.GetProperty("ready").GetBoolean()) total++;
    }
    return total;
}

[MethodImpl(MethodImplOptions.NoInlining)]
static void Fail(int value) =>
    throw new InvalidOperationException("expected failure");

static long Exceptions()
{
    long caught = 0;
    for (int i = 0; i < 100_000; i++)
    {
        try { Fail(i); }
        catch (InvalidOperationException error) { caught += error.Message.Length; }
    }
    return caught;
}

static long HtmlRender()
{
    long total = 0;
    for (long i = 0; i < 100_000; i++)
    {
        string title = $"Item {i}";
        string body = $"Value <{i * 3}> & ready";
        string rendered =
            $"<article class=\"card\"><h2>{WebUtility.HtmlEncode(title)}</h2>" +
            $"<p>{WebUtility.HtmlEncode(body)}</p></article>";
        total += rendered.Length;
    }
    return total;
}
