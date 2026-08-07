namespace Aster.Web.OpenApi;

using Aster.Web;
using System.Text;
using System.Text.Json;

public struct OpenApiInfo
{
    string Title;
    string Version;
}

public OpenApiInfo OpenApiInfo(string title, string version)
{
    if (title.Length == 0)
    {
        throw new ArgumentException("OpenAPI title cannot be empty");
    }
    if (version.Length == 0)
    {
        throw new ArgumentException("OpenAPI version cannot be empty");
    }
    return new() { Title = title, Version = version };
}

private string OpenApiStatusDescription(int status)
{
    if (status == 200) { return "OK"; }
    if (status == 201) { return "Created"; }
    if (status == 202) { return "Accepted"; }
    if (status == 204) { return "No Content"; }
    if (status == 301) { return "Moved Permanently"; }
    if (status == 302) { return "Found"; }
    if (status == 304) { return "Not Modified"; }
    if (status == 400) { return "Bad Request"; }
    if (status == 401) { return "Unauthorized"; }
    if (status == 403) { return "Forbidden"; }
    if (status == 404) { return "Not Found"; }
    if (status == 405) { return "Method Not Allowed"; }
    if (status == 409) { return "Conflict"; }
    if (status == 413) { return "Payload Too Large"; }
    if (status == 415) { return "Unsupported Media Type"; }
    if (status == 422) { return "Unprocessable Content"; }
    if (status == 500) { return "Internal Server Error"; }
    if (status == 501) { return "Not Implemented"; }
    if (status == 503) { return "Service Unavailable"; }
    return "Response";
}

private bool OpenApiPathSeen(List<string> paths, string path)
{
    foreach (string existing in paths)
    {
        if (existing == path) { return true; }
    }
    return false;
}

private string OpenApiMethod(string method)
{
    string value = method.ToLowerInvariant();
    if (value == "get" || value == "put" || value == "post" ||
        value == "delete" || value == "options" || value == "head" ||
        value == "patch" || value == "trace")
    {
        return value;
    }
    throw new InvalidOperationException(
        "endpoint method cannot be represented as an OpenAPI operation"
    );
}

private void WriteOpenApiParameters(
    ref JsonWriter writer,
    RouteEndpoint endpoint
)
{
    if (endpoint.ParameterCount == 0) { return; }
    writer.WritePropertyName("parameters");
    writer.WriteStartArray();
    for (nuint index = 0; index < endpoint.ParameterCount; index += 1)
    {
        writer.WriteStartObject();
        writer.WritePropertyName("name");
        writer.WriteStringValue(endpoint.GetParameterName(index));
        writer.WritePropertyName("in");
        writer.WriteStringValue("path");
        writer.WritePropertyName("required");
        writer.WriteBooleanValue(true);
        writer.WritePropertyName("schema");
        writer.WriteStartObject();
        writer.WritePropertyName("type");
        writer.WriteStringValue("string");
        writer.WriteEndObject();
        writer.WriteEndObject();
    }
    writer.WriteEndArray();
}

private void WriteOpenApiResponses(
    ref JsonWriter writer,
    RouteEndpoint endpoint
)
{
    writer.WritePropertyName("responses");
    writer.WriteStartObject();
    if (endpoint.ProducedStatusCount == 0)
    {
        writer.WritePropertyName("200");
        writer.WriteStartObject();
        writer.WritePropertyName("description");
        writer.WriteStringValue("OK");
        writer.WriteEndObject();
    }
    else
    {
        for (nuint index = 0; index < endpoint.ProducedStatusCount; index += 1)
        {
            int status = endpoint.GetProducedStatus(index);
            writer.WritePropertyName(status.ToString());
            writer.WriteStartObject();
            writer.WritePropertyName("description");
            writer.WriteStringValue(OpenApiStatusDescription(status));
            writer.WriteEndObject();
        }
    }
    writer.WriteEndObject();
}

private void WriteOpenApiOperation(
    ref JsonWriter writer,
    RouteEndpoint endpoint
)
{
    switch (endpoint.Name)
    {
        case Option.Some(name): {
            writer.WritePropertyName("operationId");
            writer.WriteStringValue(name);
        }
        case Option.None: { }
    }
    switch (endpoint.Description)
    {
        case Option.Some(description): {
            writer.WritePropertyName("description");
            writer.WriteStringValue(description);
        }
        case Option.None: { }
    }
    if (endpoint.TagCount != 0)
    {
        writer.WritePropertyName("tags");
        writer.WriteStartArray();
        for (nuint index = 0; index < endpoint.TagCount; index += 1)
        {
            writer.WriteStringValue(endpoint.GetTag(index));
        }
        writer.WriteEndArray();
    }
    WriteOpenApiParameters(ref writer, endpoint);
    WriteOpenApiResponses(ref writer, endpoint);
}

public string GenerateOpenApi(
    EndpointDataSource endpoints,
    OpenApiInfo info
)
{
    JsonWriter writer = JsonWriter.Create();
    writer.WriteStartObject();
    writer.WritePropertyName("openapi");
    writer.WriteStringValue("3.1.0");
    writer.WritePropertyName("info");
    writer.WriteStartObject();
    writer.WritePropertyName("title");
    writer.WriteStringValue(info.Title);
    writer.WritePropertyName("version");
    writer.WriteStringValue(info.Version);
    writer.WriteEndObject();
    writer.WritePropertyName("paths");
    writer.WriteStartObject();

    List<string> paths = new();
    for (nuint endpointIndex = 0;
        endpointIndex < endpoints.Count;
        endpointIndex += 1)
    {
        RouteEndpoint first = endpoints.GetEndpoint(endpointIndex);
        string openApiPath = first.OpenApiPath;
        if (!OpenApiPathSeen(paths, openApiPath))
        {
            paths.Add(openApiPath);
            writer.WritePropertyName(openApiPath);
            writer.WriteStartObject();
            List<string> operations = new();
            for (nuint candidateIndex = 0;
                candidateIndex < endpoints.Count;
                candidateIndex += 1)
            {
                RouteEndpoint candidate = endpoints.GetEndpoint(candidateIndex);
                if (candidate.OpenApiPath == openApiPath)
                {
                    for (nuint methodIndex = 0;
                        methodIndex < candidate.MethodCount;
                        methodIndex += 1)
                    {
                        string operation = OpenApiMethod(
                            candidate.GetMethod(methodIndex)
                        );
                        if (OpenApiPathSeen(operations, operation))
                        {
                            throw new InvalidOperationException(
                                "multiple endpoints map to one OpenAPI operation"
                            );
                        }
                        operations.Add(operation);
                        writer.WritePropertyName(operation);
                        writer.WriteStartObject();
                        WriteOpenApiOperation(ref writer, candidate);
                        writer.WriteEndObject();
                    }
                }
            }
            writer.WriteEndObject();
        }
    }
    writer.WriteEndObject();
    writer.WriteEndObject();
    return writer.ToString();
}
