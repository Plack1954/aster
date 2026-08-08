private struct Owner {
    Arena arena;
}

int main() {
    Arena arena = Arena.new();
    Owner owner = Owner { arena: arena };
    Owner copied = copy(owner);
    return 0;
}
