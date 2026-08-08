private struct User {
    string name;
}

private struct Box<T> {
    T value;
}

private User CreateUser(string name) {
    return new User { name = name };
}

int main() {
    var user = new User { name = "Ada" };
    var returned = CreateUser("Grace");
    var boxed = new Box<User> { value = returned };
    var count = 1;
    var message = copy(user.name);

    count = count + 1;
    message = copy(boxed.value.name);

    Console.WriteLine(user.name);
    Console.WriteLine(count);
    Console.WriteLine(message);
    return 0;
}
