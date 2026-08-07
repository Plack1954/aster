namespace Examples.Blog.Site;

using Examples.Blog.App;
using Aster.Web;
using Aster.Web.Ssg;
using Aster.Interop;
using System.Text;

private Result<int, string> BuildSite()
{
    string outputRoot = try NativeProcessArg(0);
    BlogApplication application = try CreateApp();
    (WebApplication app, Blog state) = application;
    SiteBuild built = try SiteBuild(app, outputRoot);
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
