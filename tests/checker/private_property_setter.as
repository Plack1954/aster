private class Account
{
    public long Balance { get; private set; }

    public Account(long balance)
    {
        Balance = balance;
    }
}

int main()
{
    Account account = new Account(10);
    account.Balance = 20;
    delete account;
    return 0;
}
