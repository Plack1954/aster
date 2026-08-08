namespace System;

using System.Text;

public struct TimeSpan {
    long ticks;

    public static TimeSpan FromTicks(long value) {
        return new() { ticks = value };
    }
    public static TimeSpan FromMilliseconds(double value) {
        return TimeSpan.FromTicks(Convert.ToInt64(value * 10000.0));
    }
    public static TimeSpan FromSeconds(double value) {
        return TimeSpan.FromMilliseconds(value * 1000.0);
    }
    public static TimeSpan FromMinutes(double value) {
        return TimeSpan.FromSeconds(value * 60.0);
    }
    public static TimeSpan FromHours(double value) {
        return TimeSpan.FromMinutes(value * 60.0);
    }
    public static TimeSpan FromDays(double value) {
        return TimeSpan.FromHours(value * 24.0);
    }

    public long Ticks => ticks;
    public double TotalMilliseconds => (double)ticks / 10000.0;
    public double TotalSeconds => (double)ticks / 10000000.0;
    public double TotalMinutes => (double)ticks / 600000000.0;
    public double TotalHours => (double)ticks / 36000000000.0;
    public double TotalDays => (double)ticks / 864000000000.0;

    public readonly TimeSpan Add(TimeSpan value) {
        return TimeSpan.FromTicks(ticks + value.ticks);
    }
    public readonly TimeSpan Subtract(TimeSpan value) {
        return TimeSpan.FromTicks(ticks - value.ticks);
    }
    public readonly TimeSpan Negate() {
        return TimeSpan.FromTicks(-ticks);
    }
}
public struct DateTime {
    long unixTicks;

    public static DateTime UtcNow => new() {
        unixTicks = NativeUtcNowUnixMilliseconds() * 10000
    };
    public int Year => DatePartsFromUnixTicks(unixTicks).year;
    public int Month => DatePartsFromUnixTicks(unixTicks).month;
    public int Day => DatePartsFromUnixTicks(unixTicks).day;
    public int Hour => DatePartsFromUnixTicks(unixTicks).hour;
    public int Minute => DatePartsFromUnixTicks(unixTicks).minute;
    public int Second => DatePartsFromUnixTicks(unixTicks).second;
    public int Millisecond => DatePartsFromUnixTicks(unixTicks).millisecond;

    public readonly DateTime Add(TimeSpan value) {
        return new() { unixTicks = unixTicks + value.ticks };
    }

    public readonly DateTime AddDays(double value) {
        return this.Add(TimeSpan.FromDays(value));
    }

    public readonly string ToString() {
        return FormatDateTime(unixTicks, 0);
    }

    public static DateTime Parse(const ref string value) {
        return ParseDateTimeOffset(value).UtcDateTime;
    }

    public static bool TryParse(const ref string value, out DateTime result) {
        result = new() { unixTicks = 0 };
        try { result = DateTime.Parse(value); return true; }
        catch (Exception error) { return false; }
    }

    public readonly DateTime Subtract(TimeSpan value) {
        return new() { unixTicks = unixTicks - value.ticks };
    }

    public readonly TimeSpan Subtract(DateTime value) {
        return TimeSpan.FromTicks(unixTicks - value.unixTicks);
    }

    public DateTime Date {
        get {
            DateParts parts = DatePartsFromUnixTicks(unixTicks);
            return new()
            {
                unixTicks = DaysFromCivil(
                    parts.year, parts.month, parts.day
                ) * 864000000000
            };
        }
    }

    public TimeSpan TimeOfDay {
        get {
            DateParts parts = DatePartsFromUnixTicks(unixTicks);
            long dayTicks = DaysFromCivil(
                parts.year, parts.month, parts.day
            ) * 864000000000;
            return TimeSpan.FromTicks(unixTicks - dayTicks);
        }
    }
}
public struct DateTimeOffset {
    long unixTicks;
    short offsetMinutes;

    public static DateTimeOffset UtcNow => new() {
        unixTicks = NativeUtcNowUnixMilliseconds() * 10000,
        offsetMinutes = 0
    };

    public static DateTimeOffset FromUnixTimeMilliseconds(long milliseconds) {
        return new() { unixTicks = milliseconds * 10000, offsetMinutes = 0 };
    }

    public static DateTimeOffset FromUnixTimeSeconds(long seconds) {
        return DateTimeOffset.FromUnixTimeMilliseconds(seconds * 1000);
    }

    public readonly long ToUnixTimeMilliseconds() {
        return unixTicks / 10000;
    }

    public readonly long ToUnixTimeSeconds() {
        return unixTicks / 10000000;
    }

    public DateTime UtcDateTime => new() { unixTicks = unixTicks };
    public TimeSpan Offset => TimeSpan.FromMinutes((double)offsetMinutes);

    public readonly DateTimeOffset ToOffset(TimeSpan offset) {
        if (offset.ticks % 600000000 != 0) {
            throw new ArgumentException(
                "Offset must be specified in whole minutes."
            );
        }
        long minutes = offset.ticks / 600000000;
        if (minutes < -840 || minutes > 840) {
            throw new ArgumentException(
                "Offset must be within plus or minus 14 hours."
            );
        }
        return new() { unixTicks = unixTicks, offsetMinutes = (short)minutes };
    }

    public readonly DateTimeOffset Add(TimeSpan value) {
        return new() {
            unixTicks = unixTicks + value.ticks,
            offsetMinutes = offsetMinutes
        };
    }

    public readonly DateTimeOffset AddDays(double value) {
        return this.Add(TimeSpan.FromDays(value));
    }

    private readonly DateParts LocalParts() {
        return DatePartsFromUnixTicks(
            unixTicks + (long)offsetMinutes * 600000000
        );
    }

    public int Year => this.LocalParts().year;
    public int Month => this.LocalParts().month;
    public int Day => this.LocalParts().day;
    public int Hour => this.LocalParts().hour;
    public int Minute => this.LocalParts().minute;
    public int Second => this.LocalParts().second;
    public int Millisecond => this.LocalParts().millisecond;

    public readonly string ToString() {
        return FormatDateTime(unixTicks, offsetMinutes);
    }

    public static DateTimeOffset Parse(const ref string value) {
        return ParseDateTimeOffset(value);
    }

    public static bool TryParse(
        const ref string value, out DateTimeOffset result) {
        result = DateTimeOffset.FromUnixTimeMilliseconds(0);
        try { result = DateTimeOffset.Parse(value); return true; }
        catch (Exception error) { return false; }
    }

    public readonly DateTimeOffset Subtract(TimeSpan value) {
        return new() {
            unixTicks = unixTicks - value.ticks,
            offsetMinutes = offsetMinutes
        };
    }

    public readonly TimeSpan Subtract(DateTimeOffset value) {
        return TimeSpan.FromTicks(unixTicks - value.unixTicks);
    }
}
public struct DateOnly {
    int dayNumber;

    private readonly DateParts LocalParts() {
        return DatePartsFromUnixTicks(
            ((long)dayNumber - 719162) * 864000000000
        );
    }

    public static DateOnly FromDateTime(DateTime dateTime) {
        DateParts parts = DatePartsFromUnixTicks(dateTime.unixTicks);
        return new() {
            dayNumber = (int)(
                DaysFromCivil(parts.year, parts.month, parts.day) + 719162
            )
        };
    }

    public static DateOnly Parse(const ref string value) {
        return ParseDateOnly(value);
    }

    public static bool TryParse(const ref string value, out DateOnly result) {
        result = new() { dayNumber = 0 };
        try { result = DateOnly.Parse(value); return true; }
        catch (Exception error) { return false; }
    }

    public int Year => this.LocalParts().year;
    public int Month => this.LocalParts().month;
    public int Day => this.LocalParts().day;
    public int DayNumber => dayNumber;

    public readonly DateOnly AddDays(int value) {
        int result = dayNumber + value;
        if (result < 0 || result > 3652058) {
            throw new OverflowException(
                "DateOnly value is outside the supported range"
            );
        }
        return new() { dayNumber = result };
    }

    public readonly string ToString() {
        DateParts parts = this.LocalParts();
        StringBuilder builder = new();
        AppendFourDigits(ref builder, parts.year);
        builder.AppendByte(45);
        AppendTwoDigits(ref builder, parts.month);
        builder.AppendByte(45);
        AppendTwoDigits(ref builder, parts.day);
        return builder.ToString();
    }

    public readonly DateTime ToDateTime(TimeOnly time) {
        return new() {
            unixTicks = ((long)dayNumber - 719162) * 864000000000 +
                time.ticks
        };
    }
}

public struct TimeOnly {
    long ticks;

    public static TimeOnly FromDateTime(DateTime dateTime) {
        long result = dateTime.unixTicks % 864000000000;
        if (result < 0) { result += 864000000000; }
        return new() { ticks = result };
    }

    public static TimeOnly FromTimeSpan(TimeSpan timeSpan) {
        if (timeSpan.ticks < 0 || timeSpan.ticks >= 864000000000) {
            throw new ArgumentException(
                "TimeSpan must describe a time within one day"
            );
        }
        return new() { ticks = timeSpan.ticks };
    }

    public static TimeOnly Parse(const ref string value) {
        return ParseTimeOnly(value);
    }

    public static bool TryParse(const ref string value, out TimeOnly result) {
        result = new() { ticks = 0 };
        try { result = TimeOnly.Parse(value); return true; }
        catch (Exception error) { return false; }
    }

    public int Hour => (int)(ticks / 36000000000);
    public int Minute => (int)(ticks / 600000000 % 60);
    public int Second => (int)(ticks / 10000000 % 60);
    public int Millisecond => (int)(ticks / 10000 % 1000);
    public long Ticks => ticks;

    public readonly TimeSpan ToTimeSpan() {
        return TimeSpan.FromTicks(ticks);
    }

    public readonly TimeOnly Add(TimeSpan value) {
        long result = (ticks + value.ticks) % 864000000000;
        if (result < 0) { result += 864000000000; }
        return new() { ticks = result };
    }

    public readonly string ToString() {
        StringBuilder builder = new();
        AppendTwoDigits(ref builder, this.Hour);
        builder.AppendByte(58);
        AppendTwoDigits(ref builder, this.Minute);
        builder.AppendByte(58);
        AppendTwoDigits(ref builder, this.Second);
        builder.AppendByte(46);
        AppendThreeDigits(ref builder, this.Millisecond);
        return builder.ToString();
    }
}

private struct DateParts
{
    int year;
    int month;
    int day;
    int hour;
    int minute;
    int second;
    int millisecond;
}

private extern long NativeUtcNowUnixMilliseconds();

private long DaysFromCivil(int year, int month, int day)
{
    int adjustedYear = year - (month <= 2 ? 1 : 0);
    long era = (long)adjustedYear / 400;
    long yearOfEra = (long)adjustedYear - era * 400;
    int shiftedMonth = month + (month > 2 ? -3 : 9);
    long dayOfYear =
        (153 * (long)shiftedMonth + 2) / 5 + (long)day - 1;
    long dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
        yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

private DateParts DatePartsFromUnixTicks(long unixTicks)
{
    long milliseconds = unixTicks / 10000;
    long days = milliseconds / 86400000;
    long dayMilliseconds = milliseconds % 86400000;
    if (dayMilliseconds < 0)
    {
        dayMilliseconds += 86400000;
        days -= 1;
    }
    long shiftedDays = days + 719468;
    long era = shiftedDays >= 0
        ? shiftedDays / 146097
        : (shiftedDays - 146096) / 146097;
    long dayOfEra = shiftedDays - era * 146097;
    long yearOfEra = (dayOfEra - dayOfEra / 1460 +
        dayOfEra / 36524 - dayOfEra / 146096) / 365;
    int year = (int)(yearOfEra + era * 400);
    long dayOfYear = dayOfEra -
        (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    long shiftedMonth = (5 * dayOfYear + 2) / 153;
    int day = (int)(dayOfYear - (153 * shiftedMonth + 2) / 5 + 1);
    int month = (int)(shiftedMonth +
        (shiftedMonth < 10 ? (long)3 : (long)-9));
    year += month <= 2 ? 1 : 0;

    int hour = (int)(dayMilliseconds / 3600000);
    dayMilliseconds %= 3600000;
    int minute = (int)(dayMilliseconds / 60000);
    dayMilliseconds %= 60000;
    int second = (int)(dayMilliseconds / 1000);
    int millisecond = (int)(dayMilliseconds % 1000);
    return new()
    {
        year = year,
        month = month,
        day = day,
        hour = hour,
        minute = minute,
        second = second,
        millisecond = millisecond
    };
}

private void AppendTwoDigits(ref StringBuilder builder, int value)
{
    builder.AppendByte((byte)(48 + value / 10));
    builder.AppendByte((byte)(48 + value % 10));
}

private void AppendThreeDigits(ref StringBuilder builder, int value)
{
    builder.AppendByte((byte)(48 + value / 100));
    builder.AppendByte((byte)(48 + value / 10 % 10));
    builder.AppendByte((byte)(48 + value % 10));
}

private void AppendFourDigits(ref StringBuilder builder, int value)
{
    builder.AppendByte((byte)(48 + value / 1000));
    builder.AppendByte((byte)(48 + value / 100 % 10));
    builder.AppendByte((byte)(48 + value / 10 % 10));
    builder.AppendByte((byte)(48 + value % 10));
}

private string FormatDateTime(long unixTicks, short offsetMinutes)
{
    long displayTicks = unixTicks + (long)offsetMinutes * 600000000;
    DateParts parts = DatePartsFromUnixTicks(displayTicks);
    if (parts.year < 1 || parts.year > 9999)
    {
        throw new OverflowException("DateTime value is outside the supported range");
    }
    StringBuilder builder = new();
    AppendFourDigits(ref builder, parts.year);
    builder.AppendByte(45);
    AppendTwoDigits(ref builder, parts.month);
    builder.AppendByte(45);
    AppendTwoDigits(ref builder, parts.day);
    builder.AppendByte(84);
    AppendTwoDigits(ref builder, parts.hour);
    builder.AppendByte(58);
    AppendTwoDigits(ref builder, parts.minute);
    builder.AppendByte(58);
    AppendTwoDigits(ref builder, parts.second);
    builder.AppendByte(46);
    AppendThreeDigits(ref builder, parts.millisecond);
    if (offsetMinutes == 0)
    {
        builder.AppendByte(90);
    }
    else
    {
        int offset = (int)offsetMinutes;
        if (offset < 0)
        {
            builder.AppendByte(45);
            offset = -offset;
        }
        else
        {
            builder.AppendByte(43);
        }
        AppendTwoDigits(ref builder, offset / 60);
        builder.AppendByte(58);
        AppendTwoDigits(ref builder, offset % 60);
    }
    return builder.ToString();
}

private int DateDigit(const ref string value, nuint index)
{
    byte digit = value[index];
    if (digit < 48 || digit > 57)
    {
        throw new FormatException("String was not recognized as a valid DateTime.");
    }
    return (int)(digit - 48);
}

private int DateTwoDigits(const ref string value, nuint index)
{
    return DateDigit(value, index) * 10 + DateDigit(value, index + 1);
}

private bool IsLeapYear(int year)
{
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

private int DaysInMonth(int year, int month)
{
    if (month == 2) { return IsLeapYear(year) ? 29 : 28; }
    if (month == 4 || month == 6 || month == 9 || month == 11) { return 30; }
    return 31;
}

private DateTimeOffset ParseDateTimeOffset(const ref string value)
{
    bool utc = value.Length == 24 && value[23] == 90;
    bool explicitOffset = value.Length == 29 &&
        (value[23] == 43 || value[23] == 45) && value[26] == 58;
    if ((!utc && !explicitOffset) || value[4] != 45 || value[7] != 45 ||
        value[10] != 84 || value[13] != 58 || value[16] != 58 ||
        value[19] != 46)
    {
        throw new FormatException("String was not recognized as a valid DateTime.");
    }
    int year = DateDigit(value, 0) * 1000 + DateDigit(value, 1) * 100 +
        DateDigit(value, 2) * 10 + DateDigit(value, 3);
    int month = DateTwoDigits(value, 5);
    int day = DateTwoDigits(value, 8);
    int hour = DateTwoDigits(value, 11);
    int minute = DateTwoDigits(value, 14);
    int second = DateTwoDigits(value, 17);
    int millisecond = DateDigit(value, 20) * 100 +
        DateDigit(value, 21) * 10 + DateDigit(value, 22);
    int offsetHours = utc ? 0 : DateTwoDigits(value, 24);
    int offsetMinutePart = utc ? 0 : DateTwoDigits(value, 27);
    if (year < 1 || month < 1 || month > 12 || day < 1 ||
        day > DaysInMonth(year, month) || hour > 23 || minute > 59 ||
        second > 59 || offsetHours > 14 || offsetMinutePart > 59 ||
        (offsetHours == 14 && offsetMinutePart != 0))
    {
        throw new FormatException("String was not recognized as a valid DateTime.");
    }
    int offset = offsetHours * 60 + offsetMinutePart;
    if (!utc && value[23] == 45) { offset = -offset; }
    long localMilliseconds = DaysFromCivil(year, month, day) * 86400000 +
        (long)hour * 3600000 + (long)minute * 60000 +
        (long)second * 1000 + (long)millisecond;
    return new()
    {
        unixTicks = (localMilliseconds - (long)offset * 60000) * 10000,
        offsetMinutes = (short)offset
    };
}

private DateOnly ParseDateOnly(const ref string value)
{
    if (value.Length != 10 || value[4] != 45 || value[7] != 45)
    {
        throw new FormatException("String was not recognized as a valid DateOnly.");
    }
    int year = DateDigit(value, 0) * 1000 + DateDigit(value, 1) * 100 +
        DateDigit(value, 2) * 10 + DateDigit(value, 3);
    int month = DateTwoDigits(value, 5);
    int day = DateTwoDigits(value, 8);
    if (year < 1 || month < 1 || month > 12 || day < 1 ||
        day > DaysInMonth(year, month))
    {
        throw new FormatException("String was not recognized as a valid DateOnly.");
    }
    return new()
    {
        dayNumber = (int)(DaysFromCivil(year, month, day) + 719162)
    };
}

private TimeOnly ParseTimeOnly(const ref string value)
{
    bool minutePrecision = value.Length == 5 && value[2] == 58;
    bool secondPrecision = value.Length == 8 &&
        value[2] == 58 && value[5] == 58;
    bool millisecondPrecision = value.Length == 12 &&
        value[2] == 58 && value[5] == 58 && value[8] == 46;
    if (!minutePrecision && !secondPrecision && !millisecondPrecision)
    {
        throw new FormatException("String was not recognized as a valid TimeOnly.");
    }
    int hour = DateTwoDigits(value, 0);
    int minute = DateTwoDigits(value, 3);
    int second = minutePrecision ? 0 : DateTwoDigits(value, 6);
    int millisecond = millisecondPrecision ?
        DateDigit(value, 9) * 100 + DateDigit(value, 10) * 10 +
            DateDigit(value, 11) : 0;
    if (hour > 23 || minute > 59 || second > 59)
    {
        throw new FormatException("String was not recognized as a valid TimeOnly.");
    }
    return new()
    {
        ticks = (long)hour * 36000000000 +
            (long)minute * 600000000 +
            (long)second * 10000000 +
            (long)millisecond * 10000
    };
}
