# Banking System in C (DSA Project)

A menu-driven, console-based banking system written in C. The project is built around core data structures (linked lists, a stack, and sorting) with no external libraries or database. Everything runs in memory.

## Author

- **Name:** Suyash Moodbidri
- **Roll No.:** N086 | **SAP ID:** 70472500180 | **Division:** E1
- **Program:** B.Tech Computer Engineering (Integrated MBA Tech), SVKM's NMIMS MPSTME, Mumbai

## Features

- Register customers with ID, name and email validation
- Open three account types:
  - **Savings:** minimum balance of Rs.1000, 4% annual interest
  - **Current:** configurable overdraft limit, 2% annual interest
  - **Loan:** principal, annual rate and tenure in months, with an informational EMI estimate
- Deposit, withdraw, and repay loans (overpayment and overdrafts are blocked)
- Apply monthly interest (once per account per calendar month)
- Full transaction history with timestamps
- Undo the latest transaction (the original entry is kept and a reversal entry is added)
- Bank-wide summary and accounts sorted by balance
- Safe input handling (overlong input, invalid numbers and EOF are handled) and full memory cleanup on exit

## Data Structures Used

| Structure | Where it is used |
|---|---|
| Singly linked list | Customers in a bank, and accounts under each customer (head and tail pointers for O(1) insertion) |
| Doubly linked list | Transaction history of each account (`prev` / `next` pointers) |
| Stack (LIFO) | Undo mechanism, built using `undoPrev` pointers that share nodes with the transaction list |
| Insertion sort | Sorting accounts by balance (descending) on an array of pointers |
| Dynamic memory | `calloc` / `malloc` / `free` for every node, all freed on exit |

## Complexity

| Operation | Time |
|---|---|
| Add customer, account or transaction | O(1) (tail insertion) |
| Find a customer or account | O(n) linear search |
| Undo last transaction | O(1) |
| Print transaction history | O(t) |
| Sort accounts by balance | O(n²) insertion sort |

## How to Compile and Run

Requires a C compiler such as GCC.

```bash
gcc banking_dsa_simplified.c -o banking -lm
./banking
```

On Windows (MinGW):

```bash
gcc banking_dsa_simplified.c -o banking.exe
banking.exe
```

The `-lm` flag links the math library (used for the EMI calculation) and is needed on Linux and macOS.

## Menu

```
1) Deposit / repay loan
2) Withdraw
3) Apply current-month interest
4) View account and transaction history
5) Print bank summary
6) Register customer
7) Open account
8) Undo latest transaction
9) Sort accounts by balance
10) Exit
```

## Sample Flow

1. Enter the number of customers to register at the start.
2. Register each customer, then open one or more accounts for them.
3. Use the menu to deposit, withdraw, apply interest, or undo a transaction.
4. Choose option 4 to see the transaction history, where undone entries are marked `UNDONE` and reversals are marked `REVERSAL`.

## Limitations

- Data is stored only in memory and is lost when the program exits (no file or database persistence).
- Money is stored as `double`; a real system would use integer paise or a decimal type.
- Loan interest is applied manually, one month at a time, and the EMI shown is informational only.
- Linear search is used for lookups, which is fine for a small dataset but not for a large one.

## Possible Improvements

- Save and load data from files
- Use a hash table for O(1) customer and account lookup
- Replace insertion sort with merge sort or quicksort for large datasets
- Add authentication and automatic EMI deduction
