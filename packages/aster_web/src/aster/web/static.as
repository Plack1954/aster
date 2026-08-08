namespace Aster.Web.Static;

using Aster.Web;
using System.IO;
using System.Text;

private byte StaticAsciiLower(byte value)
{
    if (value >= 65 && value <= 90)
    {
        return value + 32;
    }
    return value;
}

private bool StaticEndsWith(
    const ref string value,
    const ref string suffix
)
{
    nuint valueLength = value.Length;
    nuint suffixLength = suffix.Length;
    if (suffixLength > valueLength)
    {
        return false;
    }
    nuint start = valueLength - suffixLength;
    for (nuint index = 0; index < suffixLength; index++)
    {
        if (StaticAsciiLower(StringByteAt(value, start + index)) !=
            StaticAsciiLower(StringByteAt(suffix, index)))
        {
            return false;
        }
    }
    return true;
}

public AssetKind StaticAssetKind(const ref string path)
{
    if (StaticEndsWith(path, ".js")) { return AssetKind.JavaScript; }
    if (StaticEndsWith(path, ".json")) { return AssetKind.Json; }
    if (StaticEndsWith(path, ".xml")) { return AssetKind.Xml; }
    if (StaticEndsWith(path, ".svg")) { return AssetKind.Svg; }
    if (StaticEndsWith(path, ".png")) { return AssetKind.Png; }
    if (StaticEndsWith(path, ".jpg") ||
        StaticEndsWith(path, ".jpeg")) { return AssetKind.Jpeg; }
    if (StaticEndsWith(path, ".gif")) { return AssetKind.Gif; }
    if (StaticEndsWith(path, ".webp")) { return AssetKind.WebP; }
    if (StaticEndsWith(path, ".ico")) { return AssetKind.Icon; }
    if (StaticEndsWith(path, ".woff")) { return AssetKind.Woff; }
    if (StaticEndsWith(path, ".woff2")) { return AssetKind.Woff2; }
    if (StaticEndsWith(path, ".ttf")) { return AssetKind.Ttf; }
    if (StaticEndsWith(path, ".wasm")) { return AssetKind.Wasm; }
    return AssetKind.Binary;
}

private bool StaticPathSafe(const ref string path)
{
    nuint length = path.Length;
    if (length < 2 || StringByteAt(path, 0) != 47)
    {
        return false;
    }

    nuint segmentStart = 1;
    for (nuint index = 1; index <= length; index++)
    {
        bool boundary = index == length ||
            StringByteAt(path, index) == 47;
        if (!boundary)
        {
            byte current = StringByteAt(path, index);
            if (current == 0 || current == 58 || current == 92)
            {
                return false;
            }
            continue;
        }

        nuint segmentLength = index - segmentStart;
        if (segmentLength == 0 ||
            (segmentLength == 1 &&
             StringByteAt(path, segmentStart) == 46) ||
            (segmentLength == 2 &&
             StringByteAt(path, segmentStart) == 46 &&
             StringByteAt(path, segmentStart + 1) == 46))
        {
            return false;
        }
        segmentStart = index + 1;
    }
    return true;
}

public Result<Response, string> StaticFile(
    const ref string root,
    const ref string requestPath
)
{
    if (root.Length == 0)
    {
        return Result.Err("static root must not be empty");
    }
    if (!StaticPathSafe(requestPath))
    {
        return Result.Err("unsafe static asset path");
    }

    StringBuilder path = new();
    path.Append(root);
    if (StringByteAt(root, root.Length - 1) != 47)
    {
        path.Append("/");
    }
    path.Append(StringSlice(requestPath, 1, requestPath.Length));
    string filePath = path.ToString();
    switch (NativeFileOpen(filePath, "rb"))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(file): {
            switch (NativeFileReadAll(file))
            {
                case Result.Err(error): { return Result.Err(error); }
                case Result.Ok(bytes): {
                    if (StaticEndsWith(requestPath, ".css"))
                    {
                        return Result.Ok(Results.Css(bytes));
                    }
                    return Result.Ok(Results.Asset(
                        bytes,
                        StaticAssetKind(requestPath)
                    ));
                }
            }
        }
    }
}

private bool StaticPrefixValid(const ref string prefix)
{
    nuint length = prefix.Length;
    return length >= 3 &&
        StringByteAt(prefix, 0) == 47 &&
        StringByteAt(prefix, length - 1) == 47 &&
        StaticPathSafe(StringSlice(prefix, 0, length - 1));
}

private Option<Response> StaticResponseWithCache(
    Response response,
    StaticFileOptions options
)
{
    if (options.MaxAgeSeconds == 0)
    {
        return Option.Some(response);
    }
    string value = $"public, max-age={options.MaxAgeSeconds}";
    if (options.Immutable)
    {
        value = string.Concat(value, ", immutable");
    }
    switch (ResponseHeader("Cache-Control", value))
    {
        case Result.Ok(header): {
            response.AddHeader(header);
            return Option.Some(response);
        }
        case Result.Err(error): { return Option.None; }
    }
}

private Option<Response> ResolveStatic(
    const ref string urlPrefix,
    const ref string root,
    StaticFileOptions options,
    const ref Request request
)
{
    if (!request.Path.StartsWith(urlPrefix))
    {
        return Option.None;
    }
    nuint prefixLength = urlPrefix.Length;
    if (request.Path.Length <= prefixLength)
    {
        return Option.None;
    }
    string relative = StringSlice(
        request.Path, prefixLength - 1, request.Path.Length
    );
    if (!StaticEndsWith(relative, ".css"))
    {
        StringBuilder path = new();
        path.Append(root);
        if (StringByteAt(root, root.Length - 1) != 47)
        {
            path.Append("/");
        }
        path.Append(StringSlice(relative, 1, relative.Length));
        string filePath = path.ToString();
        if (File.Exists(filePath))
        {
            Response response = Results.File(
                filePath, StaticAssetKind(relative)
            );
            return StaticResponseWithCache(response, options);
        }
        return Option.None;
    }
    switch (StaticFile(root, relative))
    {
        case Result.Ok(response): {
            return StaticResponseWithCache(response, options);
        }
        case Result.Err(error): { return Option.None; }
    }
}

private T StaticResultOrThrow<T>(Result<T, string> result)
{
    switch (result)
    {
        case Result.Ok(value): { return value; }
        case Result.Err(error): { throw new Exception(error); }
    }
}

public Result<bool, string> WebApplication.TryStatic(
    WebApplication self,
    string urlPrefix,
    string root
)
{
    return self.TryStatic(urlPrefix, root, StaticFileOptions());
}

public Result<bool, string> WebApplication.TryStatic(
    WebApplication self,
    string urlPrefix,
    string root,
    StaticFileOptions options
)
{
    if (!StaticPrefixValid(urlPrefix))
    {
        return Result.Err(
            "static URL prefix must begin and end with one safe / segment"
        );
    }
    if (root.Length == 0)
    {
        return Result.Err("static directory root cannot be empty");
    }
    if (options.MaxAgeSeconds < 0 ||
        (options.Immutable && options.MaxAgeSeconds == 0))
    {
        return Result.Err("invalid static cache policy");
    }
    self.MountStatic(urlPrefix, root, options, ResolveStatic);
    return Result.Ok(true);
}

public void WebApplication.Static(
    WebApplication self,
    string urlPrefix,
    string root
)
{
    bool ignored = StaticResultOrThrow(self.TryStatic(urlPrefix, root));
}

public void WebApplication.Static(
    WebApplication self,
    string urlPrefix,
    string root,
    StaticFileOptions options
)
{
    bool ignored = StaticResultOrThrow(
        self.TryStatic(urlPrefix, root, options)
    );
}
