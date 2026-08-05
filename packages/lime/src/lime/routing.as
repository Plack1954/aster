namespace Lime.Routing;

using System.Text;

public enum RouteSegmentKind
{
    Literal,
    Parameter,
    CatchAll,
}

public enum RouteConstraintKind
{
    None,
    Int,
    Long,
    Bool,
    Alpha,
    Min,
    Max,
    Range,
    Length,
    MinLength,
    MaxLength,
    Required,
}

struct RouteValue
{
    string name;
    string value;
}

public struct RouteValues
{
    List<RouteValue> values;

    public static RouteValues Create()
    {
        List<RouteValue> values = new();
        return new() { values = values };
    }

    public static RouteValues From(string name, string value)
    {
        RouteValues values = RouteValues.Create();
        values.Add(name, value);
        return values;
    }

    public nuint Count => this.values.Count;

    public void Add(string name, string value)
    {
        if (!ParameterNameValid(name))
        {
            throw new ArgumentException("route value name is invalid");
        }
        foreach (RouteValue existing in this.values)
        {
            if (AsciiEqualsIgnoringCase(existing.name, name))
            {
                throw new ArgumentException("route value name is duplicated");
            }
        }
        this.values.Add(new() { name = name, value = value });
    }

    public readonly Option<string> Get(string name)
    {
        foreach (RouteValue value in this.values)
        {
            if (AsciiEqualsIgnoringCase(value.name, name))
            {
                return Option.Some(value.value);
            }
        }
        return Option.None;
    }
}

struct ParsedConstraint
{
    RouteConstraintKind kind;
    long first;
    long second;
}

struct RouteSegment
{
    RouteSegmentKind kind;
    RouteConstraintKind constraintKinds[8];
    long constraintFirst[8];
    long constraintSecond[8];
    nuint constraintCount;
    string value;
    bool optional;
    bool preserveCatchAllSlashes;
}

struct ConstraintRange
{
    bool hasLower;
    long lower;
    bool hasUpper;
    long upper;
}

public struct RoutePattern
{
    string rawText;
    List<RouteSegment> segments;
    bool trailingSlash;

    public string RawText => rawText;
    public nuint SegmentCount => this.segments.Count;

    public bool HasParameters
    {
        get
        {
            foreach (RouteSegment segment in segments)
            {
                if (segment.kind != RouteSegmentKind.Literal)
                {
                    return true;
                }
            }
            return false;
        }
    }

    public nuint ParameterCount
    {
        get
        {
            nuint count = 0;
            foreach (RouteSegment segment in segments)
            {
                if (segment.kind != RouteSegmentKind.Literal)
                {
                    count += 1;
                }
            }
            return count;
        }
    }

    public readonly Option<string> SingleParameter(string path)
    {
        if (this.ParameterCount != 1 || !this.IsMatch(path))
        {
            return Option.None;
        }
        foreach (RouteSegment segment in segments)
        {
            if (segment.kind != RouteSegmentKind.Literal)
            {
                return this.Parameter(path, segment.value);
            }
        }
        return Option.None;
    }

    public static Result<RoutePattern, string> TryParse(string pattern)
    {
        if (pattern.Length == 0 || pattern[0] != 47)
        {
            return Result.Err("route pattern must begin with '/'");
        }

        List<RouteSegment> segments = new();
        if (pattern == "/")
        {
            return Result.Ok(new()
            {
                rawText = pattern,
                segments = segments,
                trailingSlash = true
            });
        }

        nuint cursor = 1;
        bool trailingSlash = false;
        while (cursor < pattern.Length)
        {
            nuint start = cursor;
            while (cursor < pattern.Length && pattern[cursor] != 47)
            {
                cursor += 1;
            }

            if (cursor == start)
            {
                return Result.Err("route pattern contains an empty segment");
            }

            string text = StringSlice(pattern, start, cursor);
            bool finalSegment = cursor == pattern.Length ||
                cursor + 1 == pattern.Length;
            switch (TryParseSegment(text, finalSegment, segments))
            {
                case Result.Err(error): { return Result.Err(error); }
                case Result.Ok(segment): { segments.Add(segment); }
            }

            if (cursor == pattern.Length)
            {
                break;
            }

            cursor += 1;
            if (cursor == pattern.Length)
            {
                trailingSlash = true;
                break;
            }
        }

        return Result.Ok(new()
        {
            rawText = pattern,
            segments = segments,
            trailingSlash = trailingSlash
        });
    }

    public readonly bool IsMatch(string path)
    {
        if (path.Length == 0 || path[0] != 47)
        {
            return false;
        }
        if (this.segments.Count == 0)
        {
            return path == "/";
        }

        nuint cursor = 1;
        for (nuint index = 0; index < this.segments.Count; index += 1)
        {
            RouteSegment segment = this.segments[index];
            bool finalSegment = index + 1 == this.segments.Count;

            if (segment.kind == RouteSegmentKind.CatchAll)
            {
                string captured = StringSlice(path, cursor, path.Length);
                if (captured.Length == 0 &&
                    segment.constraintCount == 0)
                {
                    return true;
                }
                return ConstraintMatches(segment, captured);
            }

            if (cursor == path.Length)
            {
                return (segment.optional && finalSegment) ||
                    (segment.kind == RouteSegmentKind.CatchAll &&
                     EmptyCaptureMatches(segment));
            }

            nuint start = cursor;
            while (cursor < path.Length && path[cursor] != 47)
            {
                cursor += 1;
            }
            if (cursor == start)
            {
                return false;
            }

            string value = StringSlice(path, start, cursor);
            if (segment.kind == RouteSegmentKind.Literal)
            {
                if (!AsciiEqualsIgnoringCase(segment.value, value))
                {
                    return false;
                }
            }
            else if (!ConstraintMatches(segment, value))
            {
                return false;
            }

            if (!finalSegment)
            {
                if (cursor == path.Length)
                {
                    RouteSegment next = this.segments[index + 1];
                    return index + 2 == this.segments.Count &&
                        (next.optional ||
                         (next.kind == RouteSegmentKind.CatchAll &&
                          EmptyCaptureMatches(next)));
                }
                if (cursor >= path.Length || path[cursor] != 47)
                {
                    return false;
                }
                cursor += 1;
                continue;
            }

            return cursor == path.Length ||
                (cursor + 1 == path.Length && path[cursor] == 47);
        }
        return false;
    }

    public readonly Option<string> Parameter(string path, string name)
    {
        if (!this.IsMatch(path))
        {
            return Option.None;
        }

        nuint cursor = 1;
        for (nuint index = 0; index < this.segments.Count; index += 1)
        {
            RouteSegment segment = this.segments[index];
            if (segment.kind == RouteSegmentKind.CatchAll)
            {
                if (AsciiEqualsIgnoringCase(segment.value, name))
                {
                    string captured = StringSlice(path, cursor, path.Length);
                    if (captured.Length == 0)
                    {
                        return Option.None;
                    }
                    return Option.Some(captured);
                }
                return Option.None;
            }

            if (cursor == path.Length)
            {
                if (segment.optional && segment.value == name)
                {
                    return Option.None;
                }
                return Option.None;
            }

            nuint start = cursor;
            while (cursor < path.Length && path[cursor] != 47)
            {
                cursor += 1;
            }

            if (segment.kind == RouteSegmentKind.Parameter &&
                AsciiEqualsIgnoringCase(segment.value, name))
            {
                return Option.Some(StringSlice(path, start, cursor));
            }

            if (cursor < path.Length)
            {
                cursor += 1;
            }
        }
        return Option.None;
    }

    // Negative means this pattern has higher inbound precedence. Literal
    // segments outrank constrained parameters, then unconstrained parameters,
    // constrained catch-alls, and unconstrained catch-alls.
    public readonly int ComparePrecedence(RoutePattern other)
    {
        nuint shared = this.segments.Count;
        if (other.segments.Count < shared)
        {
            shared = other.segments.Count;
        }
        for (nuint index = 0; index < shared; index += 1)
        {
            int left = InboundPrecedence(this.segments[index]);
            int right = InboundPrecedence(other.segments[index]);
            if (left < right)
            {
                return -1;
            }
            if (left > right)
            {
                return 1;
            }
        }
        if (this.segments.Count < other.segments.Count)
        {
            return -1;
        }
        if (this.segments.Count > other.segments.Count)
        {
            return 1;
        }
        return 0;
    }

    public readonly bool ConflictsWith(RoutePattern other)
    {
        if (this.ComparePrecedence(other) != 0 ||
            this.segments.Count != other.segments.Count)
        {
            return false;
        }
        for (nuint index = 0; index < this.segments.Count; index += 1)
        {
            RouteSegment left = this.segments[index];
            RouteSegment right = other.segments[index];
            if (!SegmentsOverlap(left, right))
            {
                return false;
            }
        }
        return true;
    }

    public readonly Result<string, string> GetPath(RouteValues values)
    {
        if (this.segments.Count == 0)
        {
            return Result.Ok("/");
        }

        StringBuilder output = new();
        List<string> consumed = new();
        foreach (RouteSegment segment in this.segments)
        {
            if (segment.kind == RouteSegmentKind.Literal)
            {
                output.Append("/");
                output.Append(segment.value);
                continue;
            }

            switch (values.Get(segment.value))
            {
                case Option.None: {
                    if (segment.optional)
                    {
                        continue;
                    }
                    if (segment.kind == RouteSegmentKind.CatchAll)
                    {
                        if (!EmptyCaptureMatches(segment))
                        {
                            return Result.Err(
                                $"route value '{segment.value}' is required"
                            );
                        }
                        if (output.Length == 0)
                        {
                            output.Append("/");
                        }
                        consumed.Add(segment.value);
                        continue;
                    }
                    return Result.Err(
                        $"route value '{segment.value}' is required"
                    );
                }
                case Option.Some(value): {
                    if (value.Length == 0 &&
                        segment.kind != RouteSegmentKind.CatchAll)
                    {
                        return Result.Err(
                            $"route value '{segment.value}' cannot be empty"
                        );
                    }
                    bool valid = value.Length == 0 &&
                        segment.kind == RouteSegmentKind.CatchAll
                        ? EmptyCaptureMatches(segment)
                        : ConstraintMatches(segment, value);
                    if (!valid)
                    {
                        return Result.Err(
                            $"route value '{segment.value}' violates its constraint"
                        );
                    }
                    if (value.Length > 0 ||
                        segment.kind != RouteSegmentKind.CatchAll ||
                        output.Length == 0)
                    {
                        output.Append("/");
                    }
                    AppendPercentEncoded(
                        output,
                        value,
                        segment.kind == RouteSegmentKind.CatchAll &&
                            segment.preserveCatchAllSlashes
                    );
                    consumed.Add(segment.value);
                }
            }
        }
        if (this.trailingSlash &&
            (output.Length == 0 || output.ToString().EndsWith("/") == false))
        {
            output.Append("/");
        }

        bool firstQuery = true;
        foreach (RouteValue value in values.values)
        {
            if (StringListContains(consumed, value.name))
            {
                continue;
            }
            output.Append(firstQuery ? "?" : "&");
            AppendPercentEncoded(output, value.name, false);
            output.Append("=");
            AppendPercentEncoded(output, value.value, false);
            firstQuery = false;
        }
        return Result.Ok(output.ToString());
    }
}

private Result<RouteSegment, string> TryParseSegment(
    string text,
    bool finalSegment,
    List<RouteSegment> existing
)
{
    bool opens = text.Length > 0 && text[0] == 123;
    bool closes = text.Length > 0 && text[text.Length - 1] == 125;
    if (!opens && !closes)
    {
        for (nuint index = 0; index < text.Length; index += 1)
        {
            if (text[index] == 123 || text[index] == 125)
            {
                return Result.Err("route literal contains an unmatched brace");
            }
        }
        return Result.Ok(new()
        {
            kind = RouteSegmentKind.Literal,
            constraintKinds = [
                RouteConstraintKind.None, RouteConstraintKind.None,
                RouteConstraintKind.None, RouteConstraintKind.None,
                RouteConstraintKind.None, RouteConstraintKind.None,
                RouteConstraintKind.None, RouteConstraintKind.None
            ],
            constraintFirst = [
                0, 0, 0, 0, 0, 0, 0, 0
            ],
            constraintSecond = [
                0, 0, 0, 0, 0, 0, 0, 0
            ],
            constraintCount = 0,
            value = text,
            optional = false,
            preserveCatchAllSlashes = false
        });
    }
    if (!opens || !closes || text.Length < 3)
    {
        return Result.Err("route parameter has malformed braces");
    }

    string inner = StringSlice(text, 1, text.Length - 1);
    bool catchAll = inner.Length > 0 && inner[0] == 42;
    bool preserveCatchAllSlashes = catchAll &&
        inner.Length > 1 && inner[1] == 42;
    if (catchAll)
    {
        if (!finalSegment)
        {
            return Result.Err("catch-all route parameter must be last");
        }
        inner = StringSlice(
            inner,
            preserveCatchAllSlashes ? 2 : 1,
            inner.Length
        );
    }

    bool optional = inner.Length > 0 && inner[inner.Length - 1] == 63;
    if (optional)
    {
        if (catchAll)
        {
            return Result.Err("catch-all route parameter is already optional");
        }
        if (!finalSegment)
        {
            return Result.Err("optional route parameter must be last");
        }
        inner = StringSlice(inner, 0, inner.Length - 1);
    }

    nuint separator = inner.Length;
    for (nuint index = 0; index < inner.Length; index += 1)
    {
        if (inner[index] == 58)
        {
            separator = index;
            break;
        }
    }

    string name = StringSlice(inner, 0, separator);
    if (!ParameterNameValid(name))
    {
        return Result.Err("route parameter name is invalid");
    }
    foreach (RouteSegment segment in existing)
    {
        if (segment.kind != RouteSegmentKind.Literal &&
            AsciiEqualsIgnoringCase(segment.value, name))
        {
            return Result.Err("route parameter name is duplicated");
        }
    }

    RouteConstraintKind constraintKinds[8] = [
        RouteConstraintKind.None, RouteConstraintKind.None,
        RouteConstraintKind.None, RouteConstraintKind.None,
        RouteConstraintKind.None, RouteConstraintKind.None,
        RouteConstraintKind.None, RouteConstraintKind.None
    ];
    long constraintFirst[8] = [
        0, 0, 0, 0, 0, 0, 0, 0
    ];
    long constraintSecond[8] = [
        0, 0, 0, 0, 0, 0, 0, 0
    ];
    nuint constraintCount = 0;
    if (separator < inner.Length)
    {
        nuint constraintStart = separator + 1;
        while (constraintStart <= inner.Length)
        {
            nuint constraintEnd = constraintStart;
            while (constraintEnd < inner.Length &&
                inner[constraintEnd] != 58)
            {
                constraintEnd += 1;
            }
            if (constraintEnd == constraintStart)
            {
                return Result.Err("route constraint cannot be empty");
            }
            string constraintName = StringSlice(
                inner, constraintStart, constraintEnd
            );
            switch (TryParseConstraint(constraintName))
            {
                case Result.Err(error): { return Result.Err(error); }
                case Result.Ok(parsed): {
                    if (constraintCount >= 8)
                    {
                        return Result.Err(
                            "route parameter has too many constraints"
                        );
                    }
                    constraintKinds[constraintCount] = parsed.kind;
                    constraintFirst[constraintCount] = parsed.first;
                    constraintSecond[constraintCount] = parsed.second;
                    constraintCount += 1;
                }
            }
            if (constraintEnd == inner.Length) { break; }
            constraintStart = constraintEnd + 1;
        }
    }

    RouteSegment result = new()
    {
        kind = catchAll
            ? RouteSegmentKind.CatchAll
            : RouteSegmentKind.Parameter,
        constraintKinds = constraintKinds,
        constraintFirst = constraintFirst,
        constraintSecond = constraintSecond,
        constraintCount = constraintCount,
        value = name,
        optional = optional,
        preserveCatchAllSlashes = preserveCatchAllSlashes
    };
    switch (ValidateConstraintSet(result))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(valid): { }
    }
    return Result.Ok(result);
}

private bool EmptyCaptureMatches(RouteSegment segment)
{
    return segment.constraintCount == 0 ||
        ConstraintMatches(segment, "");
}

private Result<long, string> TryConstraintInteger(
    string constraint,
    string prefix
)
{
    if (!constraint.StartsWith(prefix) || !constraint.EndsWith(")") ||
        constraint.Length <= prefix.Length + 1)
    {
        return Result.Err("route constraint argument is malformed");
    }
    string value = StringSlice(
        constraint, prefix.Length, constraint.Length - 1
    );
    long parsed = 0;
    if (!long.TryParse(value, out parsed))
    {
        return Result.Err("route constraint argument must be an integer");
    }
    return Result.Ok(parsed);
}

private Result<ParsedConstraint, string> TrySingleIntegerConstraint(
    string constraint,
    string prefix,
    RouteConstraintKind kind,
    bool nonnegative
)
{
    switch (TryConstraintInteger(constraint, prefix))
    {
        case Result.Err(error): { return Result.Err(error); }
        case Result.Ok(value): {
            if (nonnegative && value < 0)
            {
                return Result.Err(
                    "route length constraint cannot be negative"
                );
            }
            return Result.Ok(new()
            {
                kind = kind,
                first = value,
                second = 0
            });
        }
    }
}

private Result<ParsedConstraint, string> TryRangeConstraint(string constraint)
{
    string prefix = "range(";
    if (!constraint.StartsWith(prefix) || !constraint.EndsWith(")"))
    {
        return Result.Err("route range constraint is malformed");
    }
    string arguments = StringSlice(
        constraint, prefix.Length, constraint.Length - 1
    );
    nuint comma = arguments.Length;
    for (nuint index = 0; index < arguments.Length; index += 1)
    {
        if (arguments[index] == 44)
        {
            if (comma != arguments.Length)
            {
                return Result.Err(
                    "route range constraint requires two integers"
                );
            }
            comma = index;
        }
    }
    if (comma == 0 || comma + 1 >= arguments.Length)
    {
        return Result.Err("route range constraint requires two integers");
    }
    long minimum = 0;
    long maximum = 0;
    if (!long.TryParse(StringSlice(arguments, 0, comma), out minimum) ||
        !long.TryParse(
            StringSlice(arguments, comma + 1, arguments.Length), out maximum
        ))
    {
        return Result.Err("route range constraint requires two integers");
    }
    if (minimum > maximum)
    {
        return Result.Err("route range minimum exceeds its maximum");
    }
    return Result.Ok(new()
    {
        kind = RouteConstraintKind.Range,
        first = minimum,
        second = maximum
    });
}

private Result<ParsedConstraint, string> TryParseConstraint(string constraint)
{
    if (constraint == "int")
    {
        return Result.Ok(new()
        {
            kind = RouteConstraintKind.Int, first = 0, second = 0
        });
    }
    if (constraint == "long")
    {
        return Result.Ok(new()
        {
            kind = RouteConstraintKind.Long, first = 0, second = 0
        });
    }
    if (constraint == "bool")
    {
        return Result.Ok(new()
        {
            kind = RouteConstraintKind.Bool, first = 0, second = 0
        });
    }
    if (constraint == "alpha")
    {
        return Result.Ok(new()
        {
            kind = RouteConstraintKind.Alpha, first = 0, second = 0
        });
    }
    if (constraint == "required")
    {
        return Result.Ok(new()
        {
            kind = RouteConstraintKind.Required, first = 0, second = 0
        });
    }
    if (constraint.StartsWith("min("))
    {
        return TrySingleIntegerConstraint(
            constraint, "min(", RouteConstraintKind.Min, false
        );
    }
    if (constraint.StartsWith("max("))
    {
        return TrySingleIntegerConstraint(
            constraint, "max(", RouteConstraintKind.Max, false
        );
    }
    if (constraint.StartsWith("range("))
    {
        return TryRangeConstraint(constraint);
    }
    if (constraint.StartsWith("length("))
    {
        return TrySingleIntegerConstraint(
            constraint, "length(", RouteConstraintKind.Length, true
        );
    }
    if (constraint.StartsWith("minlength("))
    {
        return TrySingleIntegerConstraint(
            constraint, "minlength(", RouteConstraintKind.MinLength, true
        );
    }
    if (constraint.StartsWith("maxlength("))
    {
        return TrySingleIntegerConstraint(
            constraint, "maxlength(", RouteConstraintKind.MaxLength, true
        );
    }
    return Result.Err("route constraint is unknown");
}

private bool ParameterNameValid(string name)
{
    if (name.Length == 0)
    {
        return false;
    }
    for (nuint index = 0; index < name.Length; index += 1)
    {
        byte current = name[index];
        bool letter = (current >= 65 && current <= 90) ||
            (current >= 97 && current <= 122);
        bool digit = current >= 48 && current <= 57;
        if (!letter && !digit && current != 95)
        {
            return false;
        }
        if (index == 0 && digit)
        {
            return false;
        }
    }
    return true;
}

private ParsedConstraint ConstraintAt(RouteSegment segment, nuint index)
{
    return new()
    {
        kind = segment.constraintKinds[index],
        first = segment.constraintFirst[index],
        second = segment.constraintSecond[index]
    };
}

private bool ConstraintMatches(RouteSegment segment, string value)
{
    if (segment.constraintCount == 0) { return value.Length > 0; }
    for (nuint index = 0; index < segment.constraintCount; index += 1)
    {
        ParsedConstraint constraint = ConstraintAt(segment, index);
        if (!ConstraintMatchesOne(constraint, value)) { return false; }
    }
    return true;
}

private bool ConstraintMatchesOne(
    ParsedConstraint constraint,
    string value
)
{
    switch (constraint.kind)
    {
        case RouteConstraintKind.None: { return value.Length > 0; }
        case RouteConstraintKind.Int: {
            int parsed = 0;
            return int.TryParse(value, out parsed);
        }
        case RouteConstraintKind.Long: {
            long parsed = 0;
            return long.TryParse(value, out parsed);
        }
        case RouteConstraintKind.Bool: {
            return AsciiEqualsIgnoringCase(value, "true") ||
                AsciiEqualsIgnoringCase(value, "false");
        }
        case RouteConstraintKind.Alpha: {
            if (value.Length == 0)
            {
                return false;
            }
            for (nuint index = 0; index < value.Length; index += 1)
            {
                byte current = value[index];
                if (!((current >= 65 && current <= 90) ||
                      (current >= 97 && current <= 122)))
                {
                    return false;
                }
            }
            return true;
        }
        case RouteConstraintKind.Min: {
            long parsed = 0;
            return long.TryParse(value, out parsed) &&
                parsed >= constraint.first;
        }
        case RouteConstraintKind.Max: {
            long parsed = 0;
            return long.TryParse(value, out parsed) &&
                parsed <= constraint.first;
        }
        case RouteConstraintKind.Range: {
            long parsed = 0;
            return long.TryParse(value, out parsed) &&
                parsed >= constraint.first &&
                parsed <= constraint.second;
        }
        case RouteConstraintKind.Length: {
            return value.Length == (nuint)constraint.first;
        }
        case RouteConstraintKind.MinLength: {
            return value.Length >= (nuint)constraint.first;
        }
        case RouteConstraintKind.MaxLength: {
            return value.Length <= (nuint)constraint.first;
        }
        case RouteConstraintKind.Required: { return value.Length > 0; }
    }
}

private int InboundPrecedence(RouteSegment segment)
{
    if (segment.kind == RouteSegmentKind.Literal)
    {
        return 1;
    }
    bool constrained = segment.constraintCount > 0;
    if (segment.kind == RouteSegmentKind.Parameter)
    {
        return constrained ? 2 : 3;
    }
    return constrained ? 4 : 5;
}

private bool SegmentsOverlap(RouteSegment left, RouteSegment right)
{
    if (left.kind == RouteSegmentKind.Literal &&
        right.kind == RouteSegmentKind.Literal)
    {
        return AsciiEqualsIgnoringCase(left.value, right.value);
    }
    if (left.kind == RouteSegmentKind.Literal)
    {
        return ConstraintMatches(right, left.value);
    }
    if (right.kind == RouteSegmentKind.Literal)
    {
        return ConstraintMatches(left, right.value);
    }
    return ConstraintsOverlap(left, right);
}

private bool ConstraintsOverlap(
    RouteSegment left,
    RouteSegment right
)
{
    if (left.constraintCount == 0 || right.constraintCount == 0)
    {
        return true;
    }
    bool leftNumeric = HasNumericConstraint(left);
    bool rightNumeric = HasNumericConstraint(right);
    bool leftAlpha = HasConstraint(
        left, RouteConstraintKind.Alpha
    );
    bool rightAlpha = HasConstraint(
        right, RouteConstraintKind.Alpha
    );
    bool leftBool = HasConstraint(
        left, RouteConstraintKind.Bool
    );
    bool rightBool = HasConstraint(
        right, RouteConstraintKind.Bool
    );
    if ((leftNumeric && (rightAlpha || rightBool)) ||
        (rightNumeric && (leftAlpha || leftBool)))
    {
        return false;
    }
    if (leftNumeric && rightNumeric &&
        !RangesOverlap(
            NumericRange(left),
            NumericRange(right)
        ))
    {
        return false;
    }
    return RangesOverlap(
        EffectiveLengthRange(left),
        EffectiveLengthRange(right)
    );
}

private bool NumericConstraint(RouteConstraintKind kind)
{
    return kind == RouteConstraintKind.Int ||
        kind == RouteConstraintKind.Long ||
        kind == RouteConstraintKind.Min ||
        kind == RouteConstraintKind.Max ||
        kind == RouteConstraintKind.Range;
}

private bool LengthConstraint(RouteConstraintKind kind)
{
    return kind == RouteConstraintKind.Length ||
        kind == RouteConstraintKind.MinLength ||
        kind == RouteConstraintKind.MaxLength;
}

private bool HasConstraint(
    RouteSegment segment,
    RouteConstraintKind kind
)
{
    for (nuint index = 0; index < segment.constraintCount; index += 1)
    {
        ParsedConstraint constraint = ConstraintAt(segment, index);
        if (constraint.kind == kind) { return true; }
    }
    return false;
}

private bool HasNumericConstraint(RouteSegment segment)
{
    for (nuint index = 0; index < segment.constraintCount; index += 1)
    {
        ParsedConstraint constraint = ConstraintAt(segment, index);
        if (NumericConstraint(constraint.kind)) { return true; }
    }
    return false;
}

private void ApplyRangeLower(ref ConstraintRange range, long value)
{
    if (!range.hasLower || value > range.lower)
    {
        range.hasLower = true;
        range.lower = value;
    }
}

private void ApplyRangeUpper(ref ConstraintRange range, long value)
{
    if (!range.hasUpper || value < range.upper)
    {
        range.hasUpper = true;
        range.upper = value;
    }
}

private ConstraintRange NumericRange(RouteSegment segment)
{
    ConstraintRange range = new()
    {
        hasLower = false, lower = 0,
        hasUpper = false, upper = 0
    };
    for (nuint index = 0; index < segment.constraintCount; index += 1)
    {
        ParsedConstraint constraint = ConstraintAt(segment, index);
        switch (constraint.kind)
        {
            case RouteConstraintKind.Int: {
                ApplyRangeLower(range, -2147483648);
                ApplyRangeUpper(range, 2147483647);
            }
            case RouteConstraintKind.Min: {
                ApplyRangeLower(range, constraint.first);
            }
            case RouteConstraintKind.Max: {
                ApplyRangeUpper(range, constraint.first);
            }
            case RouteConstraintKind.Range: {
                ApplyRangeLower(range, constraint.first);
                ApplyRangeUpper(range, constraint.second);
            }
            case RouteConstraintKind.None: { }
            case RouteConstraintKind.Long: { }
            case RouteConstraintKind.Bool: { }
            case RouteConstraintKind.Alpha: { }
            case RouteConstraintKind.Length: { }
            case RouteConstraintKind.MinLength: { }
            case RouteConstraintKind.MaxLength: { }
            case RouteConstraintKind.Required: { }
        }
    }
    return range;
}

private ConstraintRange EffectiveLengthRange(RouteSegment segment)
{
    ConstraintRange range = new()
    {
        hasLower = true,
        lower = segment.kind == RouteSegmentKind.CatchAll ? 0 : 1,
        hasUpper = false,
        upper = 0
    };
    if (HasConstraint(segment, RouteConstraintKind.Bool))
    {
        ApplyRangeLower(range, 4);
        ApplyRangeUpper(range, 5);
    }
    else if (HasNumericConstraint(segment))
    {
        ApplyRangeLower(range, 1);
    }
    else if (HasConstraint(
        segment, RouteConstraintKind.Alpha
    ))
    {
        ApplyRangeLower(range, 1);
    }
    for (nuint index = 0; index < segment.constraintCount; index += 1)
    {
        ParsedConstraint constraint = ConstraintAt(segment, index);
        switch (constraint.kind)
        {
            case RouteConstraintKind.Length: {
                ApplyRangeLower(range, constraint.first);
                ApplyRangeUpper(range, constraint.first);
            }
            case RouteConstraintKind.MinLength: {
                ApplyRangeLower(range, constraint.first);
            }
            case RouteConstraintKind.MaxLength: {
                ApplyRangeUpper(range, constraint.first);
            }
            case RouteConstraintKind.Required: {
                ApplyRangeLower(range, 1);
            }
            case RouteConstraintKind.None: { }
            case RouteConstraintKind.Int: { }
            case RouteConstraintKind.Long: { }
            case RouteConstraintKind.Bool: { }
            case RouteConstraintKind.Alpha: { }
            case RouteConstraintKind.Min: { }
            case RouteConstraintKind.Max: { }
            case RouteConstraintKind.Range: { }
        }
    }
    return range;
}

private bool RangeIsEmpty(ConstraintRange range)
{
    return range.hasLower && range.hasUpper && range.lower > range.upper;
}

private bool RangesOverlap(ConstraintRange left, ConstraintRange right)
{
    if (left.hasUpper && right.hasLower && left.upper < right.lower)
    {
        return false;
    }
    if (right.hasUpper && left.hasLower && right.upper < left.lower)
    {
        return false;
    }
    return true;
}

private Result<bool, string> ValidateConstraintSet(
    RouteSegment segment
)
{
    bool numeric = HasNumericConstraint(segment);
    bool alpha = HasConstraint(segment, RouteConstraintKind.Alpha);
    bool boolean = HasConstraint(segment, RouteConstraintKind.Bool);
    if (numeric && (alpha || boolean))
    {
        return Result.Err(
            "route parameter combines incompatible constraints"
        );
    }
    if (RangeIsEmpty(NumericRange(segment)))
    {
        return Result.Err("route numeric constraints do not overlap");
    }
    if (RangeIsEmpty(EffectiveLengthRange(segment)))
    {
        return Result.Err("route length constraints do not overlap");
    }
    return Result.Ok(true);
}

private bool StringListContains(List<string> values, string wanted)
{
    foreach (string value in values)
    {
        if (AsciiEqualsIgnoringCase(value, wanted))
        {
            return true;
        }
    }
    return false;
}

private byte AsciiLower(byte value)
{
    if (value >= 65 && value <= 90)
    {
        return value + 32;
    }
    return value;
}

private bool AsciiEqualsIgnoringCase(string left, string right)
{
    if (left.Length != right.Length)
    {
        return false;
    }
    for (nuint index = 0; index < left.Length; index += 1)
    {
        if (AsciiLower(left[index]) != AsciiLower(right[index]))
        {
            return false;
        }
    }
    return true;
}

private bool IsUnreserved(byte value)
{
    return (value >= 65 && value <= 90) ||
        (value >= 97 && value <= 122) ||
        (value >= 48 && value <= 57) ||
        value == 45 || value == 46 || value == 95 || value == 126;
}

private void AppendPercentEncoded(
    ref StringBuilder output,
    string value,
    bool preserveSlash
)
{
    string hex = "0123456789ABCDEF";
    for (nuint index = 0; index < value.Length; index += 1)
    {
        byte current = value[index];
        if (IsUnreserved(current) || (preserveSlash && current == 47))
        {
            output.Append(StringSlice(value, index, index + 1));
            continue;
        }
        output.Append("%");
        output.Append(StringSlice(
            hex, (nuint)(current >> 4), (nuint)(current >> 4) + 1
        ));
        output.Append(StringSlice(
            hex, (nuint)(current & 15), (nuint)(current & 15) + 1
        ));
    }
}
