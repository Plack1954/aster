namespace Docs.Config;

using System.IO;
using System.Text;

public struct ServerConfig {
    string address;
    long port;
    long maxHeaderBytes;
    long maxBodyBytes;
    long timeoutMs;
}

public ServerConfig DevelopmentConfig() {
    return new ServerConfig {
        address = "127.0.0.1",
        port = 0,
        maxHeaderBytes = 16384,
        maxBodyBytes = 1048576,
        timeoutMs = 5000,
    };
}

public Result<string, IoError> LoadText(string path) {
    NativeHandle file = try NativeFileOpen(path, "rb");
    string contents = try NativeFileReadAll(file);
    return Result.Ok(contents);
}

private bool IsSpace(byte value) {
    if (value == 32) {
        return true;
    }
    if (value == 9) {
        return true;
    }
    return value == 13;
}

private string trim(string value) {
    nuint start = 0;
    nuint end = value.Length;
    bool scanningStart = true;
    while (scanningStart) {
        if (start >= end) {
            scanningStart = false;
        } else if (IsSpace(StringByteAt(value, start))) {
            start = start + 1;
        } else {
            scanningStart = false;
        }
    }
    bool scanningEnd = true;
    while (scanningEnd) {
        if (end <= start) {
            scanningEnd = false;
        } else if (IsSpace(StringByteAt(value, end - 1))) {
            end = end - 1;
        } else {
            scanningEnd = false;
        }
    }
    return StringSlice(value, start, end);
}

private Result<long, string> ParseNumber(string value) {
    string text = trim(value);
    nuint length = text.Length;
    if (length == 0) {
        return Result.Err("expected a decimal integer");
    }
    nuint index = 0;
    long result = 0;
    while (index < length) {
        byte byte = StringByteAt(text, index);
        if (byte < 48) {
            return Result.Err("invalid decimal integer");
        }
        if (byte > 57) {
            return Result.Err("invalid decimal integer");
        }
        result = result * 10 + (long)(byte - 48);
        index = index + 1;
    }
    return Result.Ok(result);
}

public Result<ServerConfig, string> ParseConfig(string source) {
    long port = 0;
    long maxHeaderBytes = 16384;
    long maxBodyBytes = 1048576;
    long timeoutMs = 5000;
    nuint length = source.Length;
    nuint cursor = 0;

    while (cursor < length) {
        nuint end = cursor;
        bool findingEnd = true;
        while (findingEnd) {
            if (end >= length) {
                findingEnd = false;
            } else if (StringByteAt(source, end) == 10) {
                findingEnd = false;
            } else {
                end = end + 1;
            }
        }
        string line = trim(StringSlice(source, cursor, end));
        nuint lineLength = line.Length;
        if (lineLength > 0) {
          if (StringByteAt(line, 0) != 35) {
            nuint equals = 0;
            bool found = false;
            while (!found) {
                if (equals >= lineLength) {
                    return Result.Err(
                        "configuration line requires `key=value`"
                    );
                } else if (StringByteAt(line, equals) == 61) {
                  found = true;
                } else {
                  equals = equals + 1;
                }
            }
            string key = trim(StringSlice(line, 0, equals));
            string value =
                trim(StringSlice(line, equals + 1, lineLength));
            if (key == "address") {
                if (value != "127.0.0.1") {
                    return Result.Err(
                        "docs server address must be 127.0.0.1"
                    );
                }
            } else if (key == "port") {
                port = try ParseNumber(value);
            } else if (key == "max_header_bytes") {
                maxHeaderBytes = try ParseNumber(value);
            } else if (key == "max_body_bytes") {
                maxBodyBytes = try ParseNumber(value);
            } else if (key == "timeout_ms") {
                timeoutMs = try ParseNumber(value);
            } else {
                return Result.Err("unknown configuration key");
            }
          }
        }
        if (end < length) {
            cursor = end + 1;
        } else {
            cursor = length;
        }
    }

    if (port > 65535) {
        return Result.Err("configuration value is outside its limit");
    }
    if (maxHeaderBytes < 1024) {
        return Result.Err("configuration value is outside its limit");
    }
    if (maxHeaderBytes > 65536) {
        return Result.Err("configuration value is outside its limit");
    }
    if (maxBodyBytes > 16777216) {
        return Result.Err("configuration value is outside its limit");
    }
    if (timeoutMs < 1) {
        return Result.Err("configuration value is outside its limit");
    }
    if (timeoutMs > 300000) {
        return Result.Err("configuration value is outside its limit");
    }
    return Result.Ok(new ServerConfig {
        address = "127.0.0.1",
        port = port,
        maxHeaderBytes = maxHeaderBytes,
        maxBodyBytes = maxBodyBytes,
        timeoutMs = timeoutMs,
    });
}

public Result<ServerConfig, string> LoadConfig(string path) {
    NativeHandle file = try NativeFileOpen(path, "rb");
    string contents = try NativeFileReadAll(file);
    string source = contents;
    return ParseConfig(source);
}
