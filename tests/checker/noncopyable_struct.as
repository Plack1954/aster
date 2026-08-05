struct Owner {
    Arena arena;
}

int main() {
    Arena arena = Arena.new();
    Owner owner = Owner { arena: arena };
    Owner copied = owner;
    return 0;
}
