namespace Aster.Web.Forwarding;

using System.Text;

public enum ForwardedHeaders
{
    None,
    XForwardedFor,
    XForwardedHost,
    XForwardedProto,
    All,
}

public struct ForwardedHeadersOptions
{
    ForwardedHeaders ForwardedHeaders;
    long ForwardLimit;
    List<string> KnownProxies;
}

public ForwardedHeadersOptions ForwardedHeadersOptions()
{
    List<string> knownProxies = new();
    return new()
    {
        ForwardedHeaders = ForwardedHeaders.None,
        ForwardLimit = 1,
        KnownProxies = knownProxies
    };
}

public struct ForwardedOrigin
{
    string Host;
    string Scheme;
    string RemoteIpAddress;
}

private bool UsesForwardedFor(ForwardedHeaders value)
{
    return value == ForwardedHeaders.XForwardedFor ||
        value == ForwardedHeaders.All;
}

private bool UsesForwardedHost(ForwardedHeaders value)
{
    return value == ForwardedHeaders.XForwardedHost ||
        value == ForwardedHeaders.All;
}

private bool UsesForwardedProto(ForwardedHeaders value)
{
    return value == ForwardedHeaders.XForwardedProto ||
        value == ForwardedHeaders.All;
}

public void ValidateForwardedHeadersOptions(
    const ref ForwardedHeadersOptions options
)
{
    if (options.ForwardLimit < 1)
    {
        throw new ArgumentException(
            "ForwardLimit must be at least one."
        );
    }
    if (options.ForwardedHeaders != ForwardedHeaders.None &&
        options.KnownProxies.Count == 0)
    {
        throw new InvalidOperationException(
            "Forwarded headers require at least one known proxy."
        );
    }
}

private bool IsKnownProxy(
    const ref List<string> knownProxies,
    const ref string address
)
{
    foreach (string knownProxy in knownProxies)
    {
        if (knownProxy == address)
        {
            return true;
        }
    }
    return false;
}

private Option<string> ForwardedValueFromRight(
    const ref string values,
    long offset
)
{
    if (offset < 0)
    {
        return Option.None;
    }
    nuint end = values.Length;
    long current = 0;
    while (true)
    {
        nuint start = end;
        while (start > 0 && StringByteAt(values, start - 1) != 44)
        {
            start -= 1;
        }
        string value = StringSlice(values, start, end).Trim();
        if (current == offset)
        {
            if (value.Length == 0)
            {
                return Option.None;
            }
            return Option.Some(value);
        }
        if (start == 0)
        {
            return Option.None;
        }
        end = start - 1;
        current += 1;
    }
    return Option.None;
}

private bool IsSafeForwardedHost(const ref string value)
{
    if (value.Length == 0)
    {
        return false;
    }
    for (nuint index = 0; index < value.Length; index++)
    {
        byte current = StringByteAt(value, index);
        if (current <= 32 || current == 47 || current == 92 ||
            current == 64 || current == 127)
        {
            return false;
        }
    }
    return true;
}

public void ApplyForwardedHeadersInPlace(
    ref string host,
    ref string scheme,
    ref string remoteIpAddress,
    const ref Option<string> forwardedFor,
    const ref Option<string> forwardedHost,
    const ref Option<string> forwardedProto,
    const ref ForwardedHeadersOptions options
)
{
    if (options.ForwardedHeaders == ForwardedHeaders.None ||
        !IsKnownProxy(options.KnownProxies, remoteIpAddress))
    {
        return;
    }
    for (long hop = 0; hop < options.ForwardLimit; hop++)
    {
        if (!IsKnownProxy(options.KnownProxies, remoteIpAddress))
        {
            break;
        }

        if (UsesForwardedHost(options.ForwardedHeaders))
        {
            switch (forwardedHost)
            {
                case Option.Some(values): {
                    switch (ForwardedValueFromRight(values, hop))
                    {
                        case Option.Some(value): {
                            if (IsSafeForwardedHost(value))
                            {
                                host = value;
                            }
                        }
                        case Option.None: { }
                    }
                }
                case Option.None: { }
            }
        }

        if (UsesForwardedProto(options.ForwardedHeaders))
        {
            switch (forwardedProto)
            {
                case Option.Some(values): {
                    switch (ForwardedValueFromRight(values, hop))
                    {
                        case Option.Some(value): {
                            if (value == "http" || value == "https")
                            {
                                scheme = value;
                            }
                        }
                        case Option.None: { }
                    }
                }
                case Option.None: { }
            }
        }

        if (!UsesForwardedFor(options.ForwardedHeaders))
        {
            break;
        }
        bool foundAddress = false;
        switch (forwardedFor)
        {
            case Option.Some(values): {
                switch (ForwardedValueFromRight(values, hop))
                {
                    case Option.Some(value): {
                        remoteIpAddress = value;
                        foundAddress = true;
                    }
                    case Option.None: { }
                }
            }
            case Option.None: { }
        }
        if (!foundAddress)
        {
            break;
        }
    }
}

public ForwardedOrigin ApplyForwardedHeaders(
    ForwardedOrigin origin,
    const ref Option<string> forwardedFor,
    const ref Option<string> forwardedHost,
    const ref Option<string> forwardedProto,
    const ref ForwardedHeadersOptions options
)
{
    ApplyForwardedHeadersInPlace(
        ref origin.Host,
        ref origin.Scheme,
        ref origin.RemoteIpAddress,
        forwardedFor,
        forwardedHost,
        forwardedProto,
        options
    );
    return origin;
}
