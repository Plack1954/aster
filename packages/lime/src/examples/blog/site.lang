namespace Examples.Blog.Site;

using Examples.Blog.App;
using Lime;
using Lime.Ssg;
using Aster.Interop;
using System.Text;

private Result<int, string> BuildSite()
{
    string outputRoot = try NativeProcessArg(0);
    StatefulApp<Blog> app = try CreateApp();
    SiteBuild built = try SiteBuildStateful(app, outputRoot);
    Console.WriteLine($"Generated {built.files} files.");
    return Result.Ok(0);
}

int main()
{
    switch (BuildSite())
    {
        case Result.Ok(status): { return status; }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
