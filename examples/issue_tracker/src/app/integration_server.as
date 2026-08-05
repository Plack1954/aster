namespace App.IntegrationServer;

using App.IntegrationRoutes;
using App.Server;
using Aster.Net.Http;

int main() {
    var opened = HttpTryServerOpen(
        "127.0.0.1",
        0,
        16384,
        4096,
        5000,
        1,
    );
    switch (opened) {
        case Result.Ok(server): {
            var router = IntegrationRouter();
            return ServeRouter(
                server,
                router,
                5,
            );
        }
        case Result.Err(error): {
            Console.Error.WriteLine(error);
            return 1;
        }
    }
}
