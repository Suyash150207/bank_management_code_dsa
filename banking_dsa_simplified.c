#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <time.h>

#define SAVINGS_MIN_BALANCE 1000.0
#define SAVINGS_ANNUAL_RATE 0.04
#define CURRENT_ANNUAL_RATE 0.02
#define MAX_LOAN_RATE 100.0

typedef enum { ACC_SAVINGS = 1, ACC_CURRENT, ACC_LOAN } AccountType;
typedef enum { TX_OPEN, TX_DEPOSIT, TX_WITHDRAW, TX_INTEREST, TX_LOAN_REPAY, TX_UNDO } TxType;
enum { READ_EOF = 0, READ_OK = 1, READ_TRUNCATED = -1 };
enum { SUCCESS = 0, ERR_INPUT = 1, ERR_FUNDS, ERR_MEMORY, ERR_OVERPAYMENT, ERR_PAID, ERR_INTEREST_PERIOD };

typedef struct TransactionNode {
    int id;
    int reversedId;
    char timestamp[20];
    char description[180];
    TxType type;
    double delta;
    double previousBalance;
    int previousInterestPeriod;
    int previousLoanPeriods;
    int undone;
    struct TransactionNode *prev;
    struct TransactionNode *next;
    struct TransactionNode *undoPrev;
} TransactionNode;

typedef struct Account {
    char number[20];
    char holder[100];
    AccountType type;
    double balance;
    double overdraftLimit;
    double loanPrincipal;
    double loanRate;
    int loanTenure;
    int loanPeriods;
    int lastInterestPeriod;
    int nextTransactionId;
    int transactionCount;
    TransactionNode *txHead;
    TransactionNode *txTail;
    TransactionNode *undoTop;
    struct Account *next;
} Account;

typedef struct Customer {
    char id[30];
    char name[100];
    char email[100];
    Account *accountsHead;
    Account *accountsTail;
    int accountCount;
    struct Customer *next;
} Customer;

typedef struct Bank {
    char name[100];
    Customer *customersHead;
    Customer *customersTail;
    int customerCount;
    int accountCounter;
} Bank;

/* ---------- Input and validation ---------- */
static int readLine(char *buffer, size_t size) {
    if (!buffer || size < 2) return READ_EOF;
    if (!fgets(buffer, (int)size, stdin)) {
        buffer[0] = '\0';
        return READ_EOF;
    }

    char *newline = strchr(buffer, '\n');
    if (newline) {
        *newline = '\0';
        size_t len = strlen(buffer);
        if (len && buffer[len - 1] == '\r') buffer[len - 1] = '\0';
        return READ_OK;
    }

    /* Distinguish an exactly full line from a genuinely overlong line. */
    int ch = getchar();
    if (ch == '\n' || ch == EOF) return READ_OK;
    while (ch != '\n' && ch != EOF) ch = getchar();
    buffer[0] = '\0';
    return READ_TRUNCATED;
}

static int promptLine(const char *prompt, char *buffer, size_t size) {
    for (;;) {
        fputs(prompt, stdout);
        fflush(stdout);
        int status = readLine(buffer, size);
        if (status == READ_EOF) {
            puts("\nInput ended. Returning to the menu or exiting.");
            return READ_EOF;
        }
        if (status == READ_TRUNCATED) {
            printf("Input too long; use at most %zu characters.\n", size - 1);
            continue;
        }
        return READ_OK;
    }
}

static int parseInt(const char *s, int *out) {
    if (!s || !out) return 0;
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    errno = 0;
    char *end;
    long value = strtol(s, &end, 10);
    if (errno == ERANGE || end == s || value < INT_MIN || value > INT_MAX) return 0;
    while (isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    *out = (int)value;
    return 1;
}

static int parseDouble(const char *s, double *out) {
    if (!s || !out) return 0;
    while (isspace((unsigned char)*s)) s++;
    if (!*s) return 0;
    errno = 0;
    char *end;
    double value = strtod(s, &end);
    if (errno == ERANGE || end == s || !isfinite(value)) return 0;
    while (isspace((unsigned char)*end)) end++;
    if (*end) return 0;
    *out = value;
    return 1;
}

static int promptInt(const char *prompt, int *out) {
    char buffer[128];
    for (;;) {
        if (!promptLine(prompt, buffer, sizeof(buffer))) return 0;
        if (parseInt(buffer, out)) return 1;
        puts("Please enter a valid whole number.");
    }
}

static int promptDouble(const char *prompt, double *out) {
    char buffer[128];
    for (;;) {
        if (!promptLine(prompt, buffer, sizeof(buffer))) return 0;
        if (parseDouble(buffer, out)) return 1;
        puts("Please enter a valid numeric amount.");
    }
}

static int nonEmpty(const char *s) {
    if (!s) return 0;
    while (*s) {
        if (!isspace((unsigned char)*s)) return 1;
        s++;
    }
    return 0;
}

static int validEmail(const char *email) {
    if (!nonEmpty(email)) return 0;
    const char *at = strchr(email, '@');
    if (!at || at == email || !at[1]) return 0;
    const char *dot = strchr(at + 1, '.');
    return dot && dot > at + 1 && dot[1] != '\0';
}

static void timestampNow(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    if (!local || strftime(buffer, size, "%Y-%m-%d %H:%M:%S", local) == 0)
        snprintf(buffer, size, "unknown-time");
}

static int currentMonth(void) {
    time_t now = time(NULL);
    struct tm *local = localtime(&now);
    return local ? (local->tm_year + 1900) * 100 + local->tm_mon + 1 : 197001;
}

static const char *accountTypeName(AccountType type) {
    switch (type) {
        case ACC_SAVINGS: return "Savings";
        case ACC_CURRENT: return "Current";
        case ACC_LOAN: return "Loan";
        default: return "Unknown";
    }
}

static const char *transactionTypeName(TxType type) {
    switch (type) {
        case TX_OPEN: return "OPEN";
        case TX_DEPOSIT: return "DEPOSIT";
        case TX_WITHDRAW: return "WITHDRAW";
        case TX_INTEREST: return "INTEREST";
        case TX_LOAN_REPAY: return "LOAN_REPAY";
        case TX_UNDO: return "UNDO";
        default: return "UNKNOWN";
    }
}

/* ---------- Bank and linked-list management ---------- */
static Bank *createBank(const char *name) {
    Bank *bank = calloc(1, sizeof(*bank));
    if (!bank) return NULL;
    snprintf(bank->name, sizeof(bank->name), "%s", name);
    bank->accountCounter = 1000;
    return bank;
}

static Customer *findCustomer(Bank *bank, const char *id) {
    if (!bank || !id) return NULL;
    for (Customer *c = bank->customersHead; c; c = c->next)
        if (strcmp(c->id, id) == 0) return c;
    return NULL;
}

static Customer *registerCustomer(Bank *bank, const char *id, const char *name, const char *email) {
    if (!bank || !nonEmpty(id) || !nonEmpty(name) || !validEmail(email)) {
        puts("Invalid customer details. ID, name, and a basic valid email are required.");
        return NULL;
    }
    if (strlen(id) >= sizeof(((Customer *)0)->id) ||
        strlen(name) >= sizeof(((Customer *)0)->name) ||
        strlen(email) >= sizeof(((Customer *)0)->email)) {
        puts("One or more customer fields are too long.");
        return NULL;
    }
    if (findCustomer(bank, id)) {
        puts("That customer ID already exists.");
        return NULL;
    }

    Customer *customer = calloc(1, sizeof(*customer));
    if (!customer) {
        puts("Could not allocate memory for the customer.");
        return NULL;
    }
    snprintf(customer->id, sizeof(customer->id), "%s", id);
    snprintf(customer->name, sizeof(customer->name), "%s", name);
    snprintf(customer->email, sizeof(customer->email), "%s", email);

    if (!bank->customersHead) bank->customersHead = bank->customersTail = customer;
    else {
        bank->customersTail->next = customer;
        bank->customersTail = customer;
    }
    bank->customerCount++;
    printf("Registered customer %s. Total customers: %d\n", customer->name, bank->customerCount);
    return customer;
}

static int generateAccountNumber(Bank *bank, char *out, size_t size) {
    if (!bank || bank->accountCounter >= INT_MAX) return 0;
    int next = bank->accountCounter + 1;
    int n = snprintf(out, size, "ACC%d", next);
    if (n < 0 || (size_t)n >= size) return 0;
    bank->accountCounter = next;
    return 1;
}

static void attachAccount(Customer *customer, Account *account) {
    account->next = NULL;
    if (!customer->accountsHead) customer->accountsHead = customer->accountsTail = account;
    else {
        customer->accountsTail->next = account;
        customer->accountsTail = account;
    }
    customer->accountCount++;
}

/* Records a transaction before its related balance change is committed. */
static int recordTransaction(Account *account, TxType type, double delta,
                             double previousBalance, int reversedId,
                             const char *description) {
    if (!account || !description || account->nextTransactionId >= INT_MAX) return 0;
    TransactionNode *tx = calloc(1, sizeof(*tx));
    if (!tx) return 0;

    tx->id = ++account->nextTransactionId;
    tx->reversedId = reversedId;
    timestampNow(tx->timestamp, sizeof(tx->timestamp));
    snprintf(tx->description, sizeof(tx->description), "%s", description);
    tx->type = type;
    tx->delta = delta;
    tx->previousBalance = previousBalance;
    tx->previousInterestPeriod = account->lastInterestPeriod;
    tx->previousLoanPeriods = account->loanPeriods;
    tx->prev = account->txTail;

    if (!account->txHead) account->txHead = account->txTail = tx;
    else {
        account->txTail->next = tx;
        account->txTail = tx;
    }

    /* The chronological list and undo stack share transaction nodes. */
    if (type != TX_OPEN && type != TX_UNDO) {
        tx->undoPrev = account->undoTop;
        account->undoTop = tx;
    }
    account->transactionCount++;
    return 1;
}

static Account *allocateAccount(Bank *bank, Customer *customer, AccountType type,
                                double openingBalance, const char *openingDescription,
                                double openingDelta) {
    Account *account = calloc(1, sizeof(*account));
    if (!account) {
        puts("Could not allocate memory for the account.");
        return NULL;
    }
    if (!generateAccountNumber(bank, account->number, sizeof(account->number))) {
        free(account);
        puts("Could not generate an account number.");
        return NULL;
    }
    snprintf(account->holder, sizeof(account->holder), "%s", customer->name);
    account->type = type;
    account->balance = openingBalance;

    if (!recordTransaction(account, TX_OPEN, openingDelta, 0.0, 0, openingDescription)) {
        free(account);
        puts("Could not create the opening transaction.");
        return NULL;
    }
    attachAccount(customer, account);
    printf("Created %s account %s for %s.\n", accountTypeName(type), account->number, customer->name);
    return account;
}

static Account *createSavingsAccount(Bank *bank, Customer *customer, double deposit) {
    if (deposit < SAVINGS_MIN_BALANCE) {
        printf("Savings accounts require at least Rs.%.2f initially.\n", SAVINGS_MIN_BALANCE);
        return NULL;
    }
    char description[120];
    snprintf(description, sizeof(description), "Savings account opened; initial deposit Rs.%.2f", deposit);
    return allocateAccount(bank, customer, ACC_SAVINGS, deposit, description, deposit);
}

static Account *createCurrentAccount(Bank *bank, Customer *customer, double deposit, double overdraft) {
    if (deposit < 0.0 || overdraft < 0.0) {
        puts("Initial deposit and overdraft limit cannot be negative.");
        return NULL;
    }
    char description[150];
    snprintf(description, sizeof(description), "Current account opened; deposit Rs.%.2f; overdraft Rs.%.2f", deposit, overdraft);
    Account *account = allocateAccount(bank, customer, ACC_CURRENT, deposit, description, deposit);
    if (account) account->overdraftLimit = overdraft;
    return account;
}

static double calculateLoanEMI(const Account *account) {
    if (!account || account->type != ACC_LOAN || account->loanPrincipal <= 0.0 || account->loanTenure <= 0)
        return 0.0;
    double rate = account->loanRate / 1200.0;
    if (rate <= 0.0) return account->loanPrincipal / account->loanTenure;
    double factor = pow(1.0 + rate, account->loanTenure);
    return account->loanPrincipal * rate * factor / (factor - 1.0);
}

static Account *createLoanAccount(Bank *bank, Customer *customer,
                                  double principal, double rate, int tenure) {
    if (principal <= 0.0 || rate <= 0.0 || rate > MAX_LOAN_RATE || tenure <= 0) {
        printf("Loan amount must be positive, rate must be in (0, %.0f]%%, and tenure must be positive.\n", MAX_LOAN_RATE);
        return NULL;
    }
    char description[160];
    snprintf(description, sizeof(description), "Loan opened; principal Rs.%.2f; rate %.2f%%; tenure %d months", principal, rate, tenure);
    Account *account = allocateAccount(bank, customer, ACC_LOAN, -principal, description, -principal);
    if (account) {
        account->loanPrincipal = principal;
        account->loanRate = rate;
        account->loanTenure = tenure;
        printf("Estimated monthly EMI (informational only): Rs.%.2f\n", calculateLoanEMI(account));
    }
    return account;
}

static Account *findAccount(Customer *customer, const char *number) {
    if (!customer || !number) return NULL;
    for (Account *a = customer->accountsHead; a; a = a->next)
        if (strcmp(a->number, number) == 0) return a;
    return NULL;
}

/* ---------- Account operations ---------- */
static int deposit(Account *account, double amount);
static int repayLoan(Account *account, double amount);

static int deposit(Account *account, double amount) {
    if (!account || amount <= 0.0) {
        puts("Deposit amount must be positive.");
        return ERR_INPUT;
    }
    if (account->type == ACC_LOAN) return repayLoan(account, amount);

    double oldBalance = account->balance;
    char description[120];
    snprintf(description, sizeof(description), "Deposit Rs.%.2f", amount);
    if (!recordTransaction(account, TX_DEPOSIT, amount, oldBalance, 0, description)) {
        puts("Could not record transaction; deposit cancelled.");
        return ERR_MEMORY;
    }
    account->balance += amount;
    printf("Deposited Rs.%.2f. New balance: Rs.%.2f\n", amount, account->balance);
    return SUCCESS;
}

static int repayLoan(Account *account, double amount) {
    if (!account || account->type != ACC_LOAN || amount <= 0.0) {
        puts("A loan repayment requires a loan account and a positive amount.");
        return ERR_INPUT;
    }
    double debt = -account->balance;
    if (debt <= 1e-9) {
        puts("This loan has already been repaid.");
        return ERR_PAID;
    }
    if (amount > debt + 1e-9) {
        printf("Repayment exceeds the outstanding debt of Rs.%.2f.\n", debt);
        return ERR_OVERPAYMENT;
    }
    double oldBalance = account->balance;
    char description[140];
    snprintf(description, sizeof(description), "Manual loan repayment Rs.%.2f", amount);
    if (!recordTransaction(account, TX_LOAN_REPAY, amount, oldBalance, 0, description)) {
        puts("Could not record transaction; repayment cancelled.");
        return ERR_MEMORY;
    }
    account->balance += amount;
    if (fabs(account->balance) < 1e-9) account->balance = 0.0;
    printf("Repayment recorded. Remaining debt: Rs.%.2f\n", -account->balance);
    return SUCCESS;
}

static int withdraw(Account *account, double amount) {
    if (!account || amount <= 0.0) {
        puts("Withdrawal amount must be positive.");
        return ERR_INPUT;
    }
    if (account->type == ACC_LOAN) {
        puts("Withdrawals are not allowed on loan accounts. Use loan repayment instead.");
        return ERR_INPUT;
    }
    if (account->type == ACC_SAVINGS && account->balance - amount < SAVINGS_MIN_BALANCE) {
        printf("Withdrawal denied: savings balance must remain at least Rs.%.2f.\n", SAVINGS_MIN_BALANCE);
        return ERR_FUNDS;
    }
    if (account->type == ACC_CURRENT && amount > account->balance + account->overdraftLimit) {
        printf("Withdrawal denied: overdraft limit is Rs.%.2f.\n", account->overdraftLimit);
        return ERR_FUNDS;
    }

    double oldBalance = account->balance;
    char description[120];
    snprintf(description, sizeof(description), "Withdrawal Rs.%.2f", amount);
    if (!recordTransaction(account, TX_WITHDRAW, -amount, oldBalance, 0, description)) {
        puts("Could not record transaction; withdrawal cancelled.");
        return ERR_MEMORY;
    }
    account->balance -= amount;
    printf("Withdrawn Rs.%.2f. New balance: Rs.%.2f\n", amount, account->balance);
    return SUCCESS;
}

static int applyInterest(Account *account) {
    if (!account) return ERR_INPUT;
    int period = currentMonth();
    if (account->lastInterestPeriod == period) {
        puts("Interest has already been processed for this account this calendar month.");
        return ERR_INTEREST_PERIOD;
    }

    double oldBalance = account->balance;
    double interest = 0.0;
    char description[160];
    if (account->type == ACC_SAVINGS) {
        interest = account->balance * SAVINGS_ANNUAL_RATE / 12.0;
    } else if (account->type == ACC_CURRENT) {
        if (account->balance <= 0.0) {
            puts("No interest applies to a current account with a nonpositive balance.");
            return SUCCESS;
        }
        interest = account->balance * CURRENT_ANNUAL_RATE / 12.0;
    } else {
        if (account->balance >= -1e-9) {
            puts("This loan has already been repaid; no interest charged.");
            return SUCCESS;
        }
        if (account->loanPeriods >= account->loanTenure) {
            puts("All configured loan interest periods have been processed.");
            return ERR_INTEREST_PERIOD;
        }
        interest = (-account->balance) * account->loanRate / 1200.0;
        snprintf(description, sizeof(description), "Manual monthly loan interest for %04d-%02d", period / 100, period % 100);
        if (!recordTransaction(account, TX_INTEREST, -interest, oldBalance, 0, description)) {
            puts("Could not record transaction; interest cancelled.");
            return ERR_MEMORY;
        }
        account->balance -= interest;
        account->lastInterestPeriod = period;
        account->loanPeriods++;
        printf("Accrued loan interest: Rs.%.2f. Debt now: Rs.%.2f\n", interest, -account->balance);
        return SUCCESS;
    }

    snprintf(description, sizeof(description), "Monthly interest for %04d-%02d", period / 100, period % 100);
    if (!recordTransaction(account, TX_INTEREST, interest, oldBalance, 0, description)) {
        puts("Could not record transaction; interest cancelled.");
        return ERR_MEMORY;
    }
    account->balance += interest;
    account->lastInterestPeriod = period;
    printf("Applied interest of Rs.%.2f. New balance: Rs.%.2f\n", interest, account->balance);
    return SUCCESS;
}

/* Undo the latest reversible transaction; keep the original history intact. */
static int undoLastTransaction(Account *account) {
    if (!account || !account->undoTop) {
        puts("No reversible transaction is available for undo.");
        return ERR_INPUT;
    }
    TransactionNode *target = account->undoTop;
    double oldBalance = account->balance;
    double restoredBalance = target->previousBalance;
    double reversalDelta = restoredBalance - oldBalance;
    char description[180];
    snprintf(description, sizeof(description), "Reversal of TX#%d (%s)", target->id, transactionTypeName(target->type));

    if (!recordTransaction(account, TX_UNDO, reversalDelta, oldBalance, target->id, description)) {
        puts("Could not record reversal; undo cancelled.");
        return ERR_MEMORY;
    }
    account->balance = restoredBalance;
    if (target->type == TX_INTEREST) {
        account->lastInterestPeriod = target->previousInterestPeriod;
        account->loanPeriods = target->previousLoanPeriods;
    }
    target->undone = 1;
    account->undoTop = target->undoPrev;
    printf("Undid TX#%d. Restored balance: Rs.%.2f\n", target->id, account->balance);
    return SUCCESS;
}

/* ---------- Display, lookup, and sorting ---------- */
static void printAccountDetails(const Account *account) {
    if (!account) return;
    printf("[%s] %s | Holder: %s | Balance: Rs.%.2f | Transactions: %d",
           accountTypeName(account->type), account->number, account->holder,
           account->balance, account->transactionCount);
    if (account->type == ACC_SAVINGS)
        printf(" | Minimum balance: Rs.%.2f", SAVINGS_MIN_BALANCE);
    else if (account->type == ACC_CURRENT)
        printf(" | Overdraft: Rs.%.2f", account->overdraftLimit);
    else
        printf(" | Principal: Rs.%.2f | Rate: %.2f%% | Interest periods: %d/%d | Estimated EMI: Rs.%.2f (informational)",
               account->loanPrincipal, account->loanRate, account->loanPeriods,
               account->loanTenure, calculateLoanEMI(account));
    putchar('\n');
}

static void printTransactionHistory(const Account *account) {
    if (!account) return;
    printf("\n--- Transaction history: %s ---\n", account->number);
    for (TransactionNode *tx = account->txHead; tx; tx = tx->next) {
        const char *status = tx->type == TX_UNDO ? "REVERSAL" : (tx->undone ? "UNDONE" : "RECORDED");
        printf("TX#%d [%s] %s %s | Delta: %+.2f | Previous balance: Rs.%.2f",
               tx->id, status, transactionTypeName(tx->type), tx->timestamp,
               tx->delta, tx->previousBalance);
        if (tx->reversedId) printf(" | Reverses TX#%d", tx->reversedId);
        printf(" | %s\n", tx->description);
    }
}

static Account *lookupAccountInteractive(Bank *bank) {
    char customerId[30], accountNumber[20];
    if (!bank->customerCount) {
        puts("No customers are registered yet.");
        return NULL;
    }
    if (!promptLine("Customer ID: ", customerId, sizeof(customerId))) return NULL;
    Customer *customer = findCustomer(bank, customerId);
    if (!customer) {
        puts("Customer not found.");
        return NULL;
    }
    if (!customer->accountsHead) {
        printf("%s has no accounts.\n", customer->name);
        return NULL;
    }
    printf("Accounts for %s:\n", customer->name);
    for (Account *a = customer->accountsHead; a; a = a->next) printAccountDetails(a);
    if (!promptLine("Account number: ", accountNumber, sizeof(accountNumber))) return NULL;
    Account *account = findAccount(customer, accountNumber);
    if (!account) puts("Account not found.");
    return account;
}

static void printBankSummary(const Bank *bank) {
    printf("\n=== %s | Customers: %d ===\n", bank->name, bank->customerCount);
    if (!bank->customersHead) {
        puts("No customers registered.");
        return;
    }
    for (Customer *c = bank->customersHead; c; c = c->next) {
        printf("Customer %s | %s | %s | Accounts: %d\n", c->id, c->name, c->email, c->accountCount);
        for (Account *a = c->accountsHead; a; a = a->next) {
            printf("  ");
            printAccountDetails(a);
        }
    }
}

static void displayAccountsSortedByBalance(const Bank *bank) {
    size_t count = 0;
    for (Customer *c = bank->customersHead; c; c = c->next)
        for (Account *a = c->accountsHead; a; a = a->next) count++;
    if (!count) {
        puts("No accounts to sort.");
        return;
    }
    if (count > (size_t)-1 / sizeof(Account *)) {
        puts("Too many accounts to sort.");
        return;
    }
    Account **array = malloc(count * sizeof(*array));
    if (!array) {
        puts("Could not allocate the sorting array.");
        return;
    }
    size_t index = 0;
    for (Customer *c = bank->customersHead; c; c = c->next)
        for (Account *a = c->accountsHead; a; a = a->next) array[index++] = a;

    /* Insertion sort: descending balance; only pointers are moved. */
    for (size_t i = 1; i < count; i++) {
        Account *key = array[i];
        size_t j = i;
        while (j > 0 && array[j - 1]->balance < key->balance) {
            array[j] = array[j - 1];
            j--;
        }
        array[j] = key;
    }
    puts("\n=== Accounts sorted by balance (highest first) ===");
    for (size_t i = 0; i < count; i++) {
        printf("%zu. ", i + 1);
        printAccountDetails(array[i]);
    }
    free(array);
}

/* ---------- Interactive workflows ---------- */
static int registerCustomerInteractive(Bank *bank) {
    char id[30], name[100], email[100];
    if (!promptLine("Customer ID: ", id, sizeof(id))) return 0;
    if (!promptLine("Customer name: ", name, sizeof(name))) return 0;
    if (!promptLine("Customer email: ", email, sizeof(email))) return 0;
    registerCustomer(bank, id, name, email);
    return 1;
}

static int createAccountInteractive(Bank *bank) {
    char id[30];
    int type;
    if (!bank->customerCount) {
        puts("Register a customer before opening an account.");
        return 1;
    }
    if (!promptLine("Customer ID for the new account: ", id, sizeof(id))) return 0;
    Customer *customer = findCustomer(bank, id);
    if (!customer) {
        puts("Customer not found.");
        return 1;
    }
    puts("Account types: 1) Savings  2) Current  3) Loan");
    if (!promptInt("Choose type: ", &type)) return 0;
    double amount, rate, overdraft;
    int tenure;
    switch (type) {
        case ACC_SAVINGS:
            if (!promptDouble("Initial deposit (minimum Rs.1000): Rs.", &amount)) return 0;
            createSavingsAccount(bank, customer, amount);
            break;
        case ACC_CURRENT:
            if (!promptDouble("Initial deposit: Rs.", &amount)) return 0;
            if (!promptDouble("Overdraft limit: Rs.", &overdraft)) return 0;
            createCurrentAccount(bank, customer, amount, overdraft);
            break;
        case ACC_LOAN:
            if (!promptDouble("Loan principal: Rs.", &amount)) return 0;
            if (!promptDouble("Annual interest rate (%): ", &rate)) return 0;
            if (!promptInt("Tenure in months: ", &tenure)) return 0;
            createLoanAccount(bank, customer, amount, rate, tenure);
            break;
        default:
            puts("Invalid account type.");
    }
    return 1;
}

/* ---------- Memory cleanup ---------- */
static void freeAllMemory(Bank *bank) {
    if (!bank) return;
    Customer *customer = bank->customersHead;
    while (customer) {
        Customer *nextCustomer = customer->next;
        Account *account = customer->accountsHead;
        while (account) {
            Account *nextAccount = account->next;
            TransactionNode *tx = account->txHead;
            while (tx) {
                TransactionNode *nextTx = tx->next;
                free(tx);
                tx = nextTx;
            }
            free(account);
            account = nextAccount;
        }
        free(customer);
        customer = nextCustomer;
    }
    free(bank);
}

int main(void) {
    Bank *bank = createBank("NMIMS National Bank");
    if (!bank) {
        fputs("Unable to initialise bank.\n", stderr);
        return EXIT_FAILURE;
    }

    int numberOfCustomers;
    if (!promptInt("Number of customers to register initially: ", &numberOfCustomers)) {
        freeAllMemory(bank);
        return EXIT_SUCCESS;
    }
    if (numberOfCustomers < 0) {
        puts("Negative count is invalid; starting with zero customers.");
        numberOfCustomers = 0;
    }
    for (int i = 0; i < numberOfCustomers; i++) {
        printf("\n--- Customer %d ---\n", i + 1);
        if (!registerCustomerInteractive(bank)) {
            freeAllMemory(bank);
            return EXIT_SUCCESS;
        }
    }

    if (bank->customerCount) {
        char answer[16];
        do {
            if (!createAccountInteractive(bank)) {
                freeAllMemory(bank);
                return EXIT_SUCCESS;
            }
            if (!promptLine("Create another account? (y/n): ", answer, sizeof(answer))) {
                freeAllMemory(bank);
                return EXIT_SUCCESS;
            }
        } while (answer[0] == 'y' || answer[0] == 'Y');
    }

    for (;;) {
        int choice;
        printf("\n========== %s: DSA Banking Menu ==========\n", bank->name);
        puts("1) Deposit / repay loan");
        puts("2) Withdraw");
        puts("3) Apply current-month interest");
        puts("4) View account and transaction history");
        puts("5) Print bank summary");
        puts("6) Register customer");
        puts("7) Open account");
        puts("8) Undo latest transaction");
        puts("9) Sort accounts by balance");
        puts("10) Exit");
        if (!promptInt("Choose an option: ", &choice)) break;

        Account *account;
        double amount;
        switch (choice) {
            case 1:
                account = lookupAccountInteractive(bank);
                if (account && promptDouble(account->type == ACC_LOAN ? "Repayment amount: Rs." : "Deposit amount: Rs.", &amount))
                    deposit(account, amount);
                break;
            case 2:
                account = lookupAccountInteractive(bank);
                if (account && promptDouble("Withdrawal amount: Rs.", &amount)) withdraw(account, amount);
                break;
            case 3:
                account = lookupAccountInteractive(bank);
                if (account) applyInterest(account);
                break;
            case 4:
                account = lookupAccountInteractive(bank);
                if (account) {
                    printAccountDetails(account);
                    printTransactionHistory(account);
                }
                break;
            case 5:
                printBankSummary(bank);
                break;
            case 6:
                if (!registerCustomerInteractive(bank)) {
                    freeAllMemory(bank);
                    return EXIT_SUCCESS;
                }
                break;
            case 7:
                if (!createAccountInteractive(bank)) {
                    freeAllMemory(bank);
                    return EXIT_SUCCESS;
                }
                break;
            case 8:
                account = lookupAccountInteractive(bank);
                if (account) undoLastTransaction(account);
                break;
            case 9:
                displayAccountsSortedByBalance(bank);
                break;
            case 10:
                freeAllMemory(bank);
                puts("Thank you for using the banking system.");
                return EXIT_SUCCESS;
            default:
                puts("Choose a menu option from 1 to 10.");
        }
    }

    freeAllMemory(bank);
    puts("Banking system closed.");
    return EXIT_SUCCESS;
}
