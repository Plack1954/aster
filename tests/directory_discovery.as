using Aster.Content;

int main()
{
    switch (DiscoverFiles("packages/lime/test_content", ".md"))
    {
        case Result.Ok(paths): {
            foreach (string path in paths)
            {
                Console.WriteLine(path);
            }
            return 0;
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
