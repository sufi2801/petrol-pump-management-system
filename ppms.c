/*
  Petrol Pump Management System (C)
  Implements:
  - Fuel inventory management (Petrol, Diesel, CNG)
  - 6 pumps (2 petrol, 2 diesel, 2 CNG) with status
  - Sales transactions with dynamic storage (calloc + realloc)
  - Transaction ID generation, vehicle & payment types, qty/amount entry
  - Automatic inventory updates, supply addition
  - Revenue tracking: fuel-wise, pump-wise, hour-wise, payment-mode-wise
  - Daily report generation
  - Proper memory allocation/deallocation
  - Uses structures, pointer-based functions, static variables where required

  Compile: gcc -std=c99 -Wall -Wextra -o petrol_pump petrol_pump.c
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --------------------------- Constants & Types --------------------------- */

#define INITIAL_TX_CAPACITY 50
#define PUMP_COUNT 6

/* Fuel price units */
#define PRICE_PETROL 102.50   /* ₹ per liter */
#define PRICE_DIESEL 88.75    /* ₹ per liter */
#define PRICE_CNG 75.00       /* ₹ per kg */

/* Opening stock */
#define OPEN_PETROL 50000.0   /* liters */
#define OPEN_DIESEL 50000.0   /* liters */
#define OPEN_CNG 20000.0      /* kg */

/* Low stock threshold */
#define LOW_STOCK_THRESHOLD 5000.0

/* Enums */
typedef enum { FUEL_PETROL = 0, FUEL_DIESEL = 1, FUEL_CNG = 2 } FuelType;
typedef enum { PUMP_ACTIVE = 0, PUMP_INACTIVE = 1, PUMP_MAINT = 2 } PumpStatus;
typedef enum { VEH_2W = 0, VEH_4W = 1, VEH_COMM = 2 } VehicleType;
typedef enum { PAY_CASH = 0, PAY_CARD = 1, PAY_WALLET = 2 } PaymentMode;

/* Structures */
typedef struct {
    FuelType type;
    double price;       /* price per unit (litre or kg) */
    double opening_stock;
    double current_stock;
    double closing_stock; /* for daily report */
} Fuel;

typedef struct {
    int pump_id;        /* unique pump id (1..PUMP_COUNT) */
    FuelType fuel_type;
    PumpStatus status;
    /* performance metrics */
    int transactions_count;
    double total_quantity; /* total quantity dispensed */
    double total_amount;   /* total revenue from this pump */
} Pump;

typedef struct {
    char txn_id[32];    /* string id */
    time_t timestamp;   /* time of transaction */
    int pump_id;
    FuelType fuel_type;
    VehicleType vehicle_type;
    double quantity;    /* quantity sold (litres or kg) */
    double amount;      /* amount billed (₹) */
    PaymentMode payment_mode;
} Transaction;

/* --------------------------- Global System State -------------------------- */

/* Fuel inventory (3 types) */
Fuel fuels[3];

/* Pumps */
Pump pumps[PUMP_COUNT];

/* Dynamic transaction array */
Transaction *transactions = NULL;
size_t tx_capacity = 0;
size_t tx_count = 0;

/* Revenue & tracking accumulators (using static variables in functions as required) */
/* But also maintain global accumulators for reporting */
double fuel_wise_quantity[3] = {0.0, 0.0, 0.0};
double fuel_wise_amount[3] = {0.0, 0.0, 0.0};

double payment_mode_amount[3] = {0.0, 0.0, 0.0}; /* cash, card, wallet */

/* Hour-wise tracking: 0..23 */
double hour_quantity[24] = {0};
double hour_amount[24] = {0};

/* Transaction ID / sequence */
static unsigned long txn_sequence = 0; /* static to persist across calls */

/* --------------------------- Utility Functions --------------------------- */

const char* fuel_name(FuelType f) {
    switch (f) {
        case FUEL_PETROL: return "Petrol";
        case FUEL_DIESEL: return "Diesel";
        default: return "CNG";
    }
}

const char* pump_status_name(PumpStatus s) {
    switch (s) {
        case PUMP_ACTIVE: return "Active";
        case PUMP_INACTIVE: return "Inactive";
        default: return "Maintenance";
    }
}

const char* vehicle_name(VehicleType v) {
    switch (v) {
        case VEH_2W: return "2-Wheeler";
        case VEH_4W: return "4-Wheeler";
        default: return "Commercial";
    }
}

const char* payment_name(PaymentMode p) {
    switch (p) {
        case PAY_CASH: return "Cash";
        case PAY_CARD: return "Credit Card";
        default: return "Digital Wallet";
    }
}

/* Format current local time as string */
void format_time_local(time_t t, char *buf, size_t bufsz) {
    struct tm *lt = localtime(&t);
    if (lt != NULL) {
        strftime(buf, bufsz, "%Y-%m-%d %H:%M:%S", lt);
    } else {
        /* fallback */
        snprintf(buf, bufsz, "unknown-time");
    }
}

/* Generate transaction ID (string) */
void generate_txn_id(char *out, size_t outsz) {
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    txn_sequence++;
    if (lt != NULL) {
        snprintf(out, outsz, "TXN%04u%02d%02d%02d%05lu",
                 (unsigned)(lt->tm_year + 1900) % 10000,
                 lt->tm_mon + 1,
                 lt->tm_mday,
                 lt->tm_hour,
                 (unsigned long)txn_sequence);
    } else {
        snprintf(out, outsz, "TXN000000000%05lu", (unsigned long)txn_sequence);
    }
}

/* Initialize fuels and pumps */
void initialize_system() {
    /* Fuels */
    fuels[FUEL_PETROL].type = FUEL_PETROL;
    fuels[FUEL_PETROL].price = PRICE_PETROL;
    fuels[FUEL_PETROL].opening_stock = OPEN_PETROL;
    fuels[FUEL_PETROL].current_stock = OPEN_PETROL;
    fuels[FUEL_PETROL].closing_stock = OPEN_PETROL;

    fuels[FUEL_DIESEL].type = FUEL_DIESEL;
    fuels[FUEL_DIESEL].price = PRICE_DIESEL;
    fuels[FUEL_DIESEL].opening_stock = OPEN_DIESEL;
    fuels[FUEL_DIESEL].current_stock = OPEN_DIESEL;
    fuels[FUEL_DIESEL].closing_stock = OPEN_DIESEL;

    fuels[FUEL_CNG].type = FUEL_CNG;
    fuels[FUEL_CNG].price = PRICE_CNG;
    fuels[FUEL_CNG].opening_stock = OPEN_CNG;
    fuels[FUEL_CNG].current_stock = OPEN_CNG;
    fuels[FUEL_CNG].closing_stock = OPEN_CNG;

    /* Pumps: assign 2 petrol, 2 diesel, 2 cng */
    for (int i = 0; i < PUMP_COUNT; ++i) {
        pumps[i].pump_id = i + 1;
        pumps[i].transactions_count = 0;
        pumps[i].total_quantity = 0.0;
        pumps[i].total_amount = 0.0;
        pumps[i].status = PUMP_ACTIVE;
        if (i < 2) pumps[i].fuel_type = FUEL_PETROL;
        else if (i < 4) pumps[i].fuel_type = FUEL_DIESEL;
        else pumps[i].fuel_type = FUEL_CNG;
    }

    /* Transactions: allocate initial capacity with calloc */
    tx_capacity = INITIAL_TX_CAPACITY;
    transactions = (Transaction*) calloc(tx_capacity, sizeof(Transaction));
    if (!transactions) {
        fprintf(stderr, "Failed to allocate initial transaction storage.\n");
        exit(EXIT_FAILURE);
    }
}

/* Free resources */
void shutdown_system() {
    if (transactions) free(transactions);
    transactions = NULL;
    tx_capacity = 0;
    tx_count = 0;
}

/* Resize transaction array when necessary (algorithm: double capacity) */
void ensure_tx_capacity() {
    if (tx_count < tx_capacity) return;
    size_t new_capacity = tx_capacity * 2;
    Transaction *new_block = (Transaction*) realloc(transactions, new_capacity * sizeof(Transaction));
    if (!new_block) {
        fprintf(stderr, "Failed to expand transaction storage to %zu records.\n", new_capacity);
        /* try small increment as fallback */
        new_capacity = tx_capacity + 50;
        new_block = (Transaction*) realloc(transactions, new_capacity * sizeof(Transaction));
        if (!new_block) {
            fprintf(stderr, "Critical: transaction storage expansion failed. Exiting.\n");
            shutdown_system();
            exit(EXIT_FAILURE);
        }
    }
    /* Zero initialize the newly allocated portion */
    memset(new_block + tx_capacity, 0, (new_capacity - tx_capacity) * sizeof(Transaction));
    transactions = new_block;
    tx_capacity = new_capacity;
}

/* Find pump index by pump id */
int pump_index_by_id(int pump_id) {
    for (int i = 0; i < PUMP_COUNT; ++i)
        if (pumps[i].pump_id == pump_id) return i;
    return -1;
}

/* Low stock check */
void check_low_stock_alerts() {
    for (int i = 0; i < 3; ++i) {
        if (fuels[i].current_stock < LOW_STOCK_THRESHOLD) {
            printf("WARNING: Low stock for %s: %.2f units left (threshold %.2f)\n",
                   fuel_name(fuels[i].type), fuels[i].current_stock, LOW_STOCK_THRESHOLD);
        }
    }
}

/* Print a receipt for a transaction */
void print_receipt(const Transaction *t) {
    char timestr[64];
    format_time_local(t->timestamp, timestr, sizeof(timestr));
    printf("\n------------------- FUEL RECEIPT -------------------\n");
    printf("Transaction ID : %s\n", t->txn_id);
    printf("Date & Time    : %s\n", timestr);
    printf("Pump ID        : %d\n", t->pump_id);
    printf("Fuel Type      : %s\n", fuel_name(t->fuel_type));
    printf("Vehicle Type   : %s\n", vehicle_name(t->vehicle_type));
    printf("Quantity       : %.3f %s\n", t->quantity, (t->fuel_type == FUEL_CNG ? "kg" : "liters"));
    printf("Amount (INR)   : %.2f\n", t->amount);
    printf("Payment Mode   : %s\n", payment_name(t->payment_mode));
    printf("----------------------------------------------------\n\n");
}

/* --------------------------- Core Operations ----------------------------- */

/* Create & store a transaction. Handles dynamic allocation and updates all stats. */
void record_transaction(const Transaction *tx) {
    ensure_tx_capacity();
    transactions[tx_count] = *tx; /* struct copy */
    tx_count++;

    /* Update pump stats */
    int pidx = pump_index_by_id(tx->pump_id);
    if (pidx >= 0) {
        pumps[pidx].transactions_count += 1;
        pumps[pidx].total_quantity += tx->quantity;
        pumps[pidx].total_amount += tx->amount;
    }

    /* Update fuel-wise accumulators */
    fuel_wise_quantity[tx->fuel_type] += tx->quantity;
    fuel_wise_amount[tx->fuel_type] += tx->amount;

    /* Update payment mode totals */
    payment_mode_amount[tx->payment_mode] += tx->amount;

    /* Update hour-wise data */
    struct tm *lt = localtime(&tx->timestamp);
    if (lt != NULL) {
        int hour = lt->tm_hour;
        hour_quantity[hour] += tx->quantity;
        hour_amount[hour] += tx->amount;
    }
}

/* Helper to clear input buffer (reads until newline or EOF) */
void clear_input_buffer(void) {
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        /* discard */
    }
}

/* Process a sale: select pump, vehicle, fuel, input qty or amount, create txn, deduct stock */
void process_sale() {
    int pump_id;
    printf("\nAvailable Pumps:\n");
    for (int i = 0; i < PUMP_COUNT; ++i) {
        printf("Pump %d - %s (%s)\n",
               pumps[i].pump_id,
               fuel_name(pumps[i].fuel_type),
               pump_status_name(pumps[i].status));
    }
    printf("Enter Pump ID to use: ");
    if (scanf("%d", &pump_id) != 1) {
        clear_input_buffer();
        printf("Invalid input.\n");
        return;
    }
    int pidx = pump_index_by_id(pump_id);
    if (pidx < 0) { printf("Invalid pump id.\n"); clear_input_buffer(); return; }
    if (pumps[pidx].status != PUMP_ACTIVE) { printf("Selected pump is not active.\n"); clear_input_buffer(); return; }

    /* vehicle selection */
    int vchoice;
    printf("Select vehicle type: 0=2-Wheeler, 1=4-Wheeler, 2=Commercial: ");
    if (scanf("%d", &vchoice) != 1 || vchoice < 0 || vchoice > 2) {
        clear_input_buffer();
        printf("Invalid.\n");
        return;
    }

    FuelType ftype = pumps[pidx].fuel_type;
    double unit_price = fuels[ftype].price;

    /* choose quantity or amount */
    int mode;
    printf("Enter input mode: 0=Quantity, 1=Amount: ");
    if (scanf("%d", &mode) != 1 || (mode != 0 && mode != 1)) {
        clear_input_buffer();
        printf("Invalid.\n");
        return;
    }

    double qty = 0.0, amt = 0.0;
    if (mode == 0) {
        printf("Enter quantity to dispense (%s): ", (ftype == FUEL_CNG ? "kg" : "liters"));
        if (scanf("%lf", &qty) != 1 || qty <= 0) {
            clear_input_buffer();
            printf("Invalid quantity.\n");
            return;
        }
        amt = qty * unit_price;
    } else {
        printf("Enter amount to spend (INR): ");
        if (scanf("%lf", &amt) != 1 || amt <= 0) {
            clear_input_buffer();
            printf("Invalid amount.\n");
            return;
        }
        qty = amt / unit_price;
    }

    /* Check stock */
    if (qty > fuels[ftype].current_stock) {
        printf("Insufficient stock. Available: %.3f units.\n", fuels[ftype].current_stock);
        clear_input_buffer();
        return;
    }

    /* Payment mode */
    int paychoice;
    printf("Payment Mode: 0=Cash, 1=Credit Card, 2=Digital Wallet: ");
    if (scanf("%d", &paychoice) != 1 || paychoice < 0 || paychoice > 2) {
        clear_input_buffer();
        printf("Invalid.\n");
        return;
    }

    /* Create transaction */
    Transaction tx;
    memset(&tx, 0, sizeof(tx));
    generate_txn_id(tx.txn_id, sizeof(tx.txn_id));
    tx.timestamp = time(NULL);
    tx.pump_id = pump_id;
    tx.fuel_type = ftype;
    tx.vehicle_type = (VehicleType)vchoice;
    tx.quantity = qty;
    tx.amount = amt;
    tx.payment_mode = (PaymentMode)paychoice;

    /* Deduct stock */
    fuels[ftype].current_stock -= qty;

    /* Update system state by recording transaction */
    record_transaction(&tx);

    /* Print receipt */
    print_receipt(&tx);

    /* Low stock check */
    check_low_stock_alerts();

    /* clear any leftover input */
    clear_input_buffer();
}

/* Add new supply to a fuel */
void add_supply() {
    printf("\nAdd supply to which fuel? 0=Petrol,1=Diesel,2=CNG: ");
    int f;
    if (scanf("%d", &f) != 1 || f < 0 || f > 2) {
        clear_input_buffer();
        printf("Invalid.\n");
        return;
    }
    double amt;
    printf("Enter quantity to add (%s): ", (f==FUEL_CNG?"kg":"liters"));
    if (scanf("%lf", &amt) != 1 || amt <= 0) {
        clear_input_buffer();
        printf("Invalid quantity.\n");
        return;
    }
    fuels[f].current_stock += amt;
    printf("Supply added. New stock for %s: %.2f\n", fuel_name(fuels[f].type), fuels[f].current_stock);
    clear_input_buffer();
}

/* Change pump status (active, inactive, maintenance) */
void change_pump_status() {
    int pid;
    printf("Enter Pump ID to change status: ");
    if (scanf("%d", &pid) != 1) {
        clear_input_buffer();
        printf("Invalid.\n");
        return;
    }
    int idx = pump_index_by_id(pid);
    if (idx < 0) { printf("Invalid pump id.\n"); clear_input_buffer(); return; }
    int s;
    printf("Select status: 0=Active,1=Inactive,2=Maintenance: ");
    if (scanf("%d", &s) != 1 || s < 0 || s > 2) {
        clear_input_buffer();
        printf("Invalid.\n");
        return;
    }
    pumps[idx].status = (PumpStatus)s;
    printf("Pump %d status set to %s\n", pid, pump_status_name(pumps[idx].status));
    clear_input_buffer();
}

/* Show pump-wise performance */
void show_pump_performance() {
    printf("\n----- Pump-wise Performance -----\n");
    for (int i = 0; i < PUMP_COUNT; ++i) {
        printf("Pump %d | Fuel: %s | Status: %s | Txns: %d | Qty: %.3f | Revenue: ₹%.2f\n",
               pumps[i].pump_id,
               fuel_name(pumps[i].fuel_type),
               pump_status_name(pumps[i].status),
               pumps[i].transactions_count,
               pumps[i].total_quantity,
               pumps[i].total_amount);
    }
}

/* Show fuel-wise summary */
void show_fuel_summary() {
    printf("\n----- Fuel-wise Summary -----\n");
    for (int i = 0; i < 3; ++i) {
        printf("%s | Opening Stock: %.2f | Current Stock: %.2f | Sold Qty: %.3f | Revenue: ₹%.2f\n",
               fuel_name(fuels[i].type),
               fuels[i].opening_stock,
               fuels[i].current_stock,
               fuel_wise_quantity[i],
               fuel_wise_amount[i]);
    }
}

/* Show hour-wise sales analysis */
void show_hour_wise_analysis() {
    printf("\n----- Hour-wise Sales Analysis -----\n");
    for (int h = 0; h < 24; ++h) {
        if (hour_quantity[h] > 0.0 || hour_amount[h] > 0.0)
            printf("Hour %02d:00 - Qty: %.3f | Revenue: ₹%.2f\n", h, hour_quantity[h], hour_amount[h]);
    }
}

/* Show payment mode breakdown */
void show_payment_breakdown() {
    printf("\n----- Payment Mode Breakdown -----\n");
    printf("Cash: ₹%.2f\n", payment_mode_amount[PAY_CASH]);
    printf("Credit Card: ₹%.2f\n", payment_mode_amount[PAY_CARD]);
    printf("Digital Wallet: ₹%.2f\n", payment_mode_amount[PAY_WALLET]);
}

/* Generate daily report */
void generate_daily_report() {
    printf("\n================= DAILY REPORT =================\n");
    /* Opening and closing stocks */
    printf("Fuel Opening & Closing Stocks:\n");
    for (int i = 0; i < 3; ++i) {
        fuels[i].closing_stock = fuels[i].current_stock;
        printf("%s: Opening: %.2f | Closing: %.2f\n",
               fuel_name(fuels[i].type),
               fuels[i].opening_stock,
               fuels[i].closing_stock);
    }
    /* Total sales quantity and amount */
    double total_qty = 0.0, total_amt = 0.0;
    for (int i = 0; i < 3; ++i) {
        total_qty += fuel_wise_quantity[i];
        total_amt += fuel_wise_amount[i];
    }
    printf("Total Sales Quantity (all fuels): %.3f\n", total_qty);
    printf("Total Revenue (all fuels): ₹%.2f\n", total_amt);
    /* Fuel-wise breakdown */
    show_fuel_summary();
    /* Number of transactions */
    printf("Number of transactions: %zu\n", tx_count);
    /* Cash/digital breakdown */
    show_payment_breakdown();
    /* Pump-wise performance */
    show_pump_performance();
    /* Hour-wise */
    show_hour_wise_analysis();
    printf("================================================\n");
}

/* List recent transactions (simple table) */
void list_transactions() {
    if (tx_count == 0) { printf("No transactions yet.\n"); return; }
    printf("\n---- Transactions (most recent first) ----\n");
    for (long i = (long)tx_count - 1; i >= 0; --i) {
        char timestr[64];
        format_time_local(transactions[i].timestamp, timestr, sizeof(timestr));
        printf("%s | %s | Pump %d | Qty: %.3f | ₹%.2f | %s\n",
               transactions[i].txn_id,
               timestr,
               transactions[i].pump_id,
               transactions[i].quantity,
               transactions[i].amount,
               payment_name(transactions[i].payment_mode));
    }
}

/* Print a sample receipt format to screen (for deliverable) */
void print_sample_receipt_format() {
    printf("\n--- Sample Receipt Format ---\n");
    printf("Station: ABC Fuel Station\n");
    printf("Address: 123 Main Road\n");
    printf("Receipt No: <TXN ID>\n");
    printf("Date/Time: <YYYY-MM-DD HH:MM:SS>\n");
    printf("Pump: <ID>\n");
    printf("Fuel: <Petrol/Diesel/CNG>\n");
    printf("Vehicle: <2W/4W/Commercial>\n");
    printf("Quantity: <x.xxx liters/kg>\n");
    printf("Rate: ₹<price> per unit\n");
    printf("Amount: ₹<xx.xx>\n");
    printf("Payment: <Cash/Card/Wallet>\n");
    printf("Thank you!\n");
    printf("-----------------------------\n");
}

/* System architecture & memory strategy printout (brief) */
void print_system_architecture() {
    printf("\n--- System Architecture & Memory Strategy ---\n");
    printf("1. Data structures:\n");
    printf("   - Fuel, Pump, Transaction (C structs)\n");
    printf("2. Transactions stored in a dynamic array (Transaction*)\n");
    printf("   - initially allocated with calloc(%d)\n", INITIAL_TX_CAPACITY);
    printf("   - expanded using realloc (doubling capacity) when full\n");
    printf("3. Pointer usage: transactions pointer manipulated by ensure_tx_capacity() and record_transaction()\n");
    printf("4. Static variables: txn_sequence (for unique ids) and other persistent counters inside functions\n");
    printf("5. Memory deallocation: transactions freed at shutdown\n");
    printf("-----------------------------------------------\n");
}

/* Advantages discussion printed to console */
void print_dynamic_allocation_advantages() {
    printf("\n--- Advantages of Dynamic Allocation for Transactions ---\n");
    printf("- Efficient initial memory usage (start small with calloc)\n");
    printf("- Can grow as needed with realloc, avoiding fixed limits\n");
    printf("- Keeps memory contiguous (when realloc succeeds), improving cache locality\n");
    printf("- Easier to manage life-cycle of daily transaction logs and free at end\n");
    printf("-----------------------------------------------\n");
}

/* Show main menu */
void show_main_menu() {
    printf("\n====== PETROL PUMP MANAGEMENT SYSTEM ======\n");
    printf("1. Process Sale (new transaction)\n");
    printf("2. Add Fuel Supply\n");
    printf("3. Change Pump Status\n");
    printf("4. List Transactions\n");
    printf("5. Generate Daily Report\n");
    printf("6. Show Pump Performance\n");
    printf("7. Show Fuel Summary\n");
    printf("8. Show Hour-wise Sales\n");
    printf("9. Show Payment Breakdown\n");
    printf("10. Print Sample Receipt Format\n");
    printf("11. Print System Architecture & Memory Strategy\n");
    printf("12. Show Advantages of Dynamic Allocation\n");
    printf("0. Exit\n");
    printf("Enter choice: ");
}

/* --------------------------- Main Program Loop --------------------------- */

int main(void) {
    initialize_system();

    int choice;
    while (1) {
        show_main_menu();
        if (scanf("%d", &choice) != 1) {
            clear_input_buffer();
            printf("Invalid input. Try again.\n");
            continue;
        }
        /* consume trailing newline to keep input state clean */
        clear_input_buffer();
        switch (choice) {
            case 1:
                process_sale();
                break;
            case 2:
                add_supply();
                break;
            case 3:
                change_pump_status();
                break;
            case 4:
                list_transactions();
                break;
            case 5:
                generate_daily_report();
                break;
            case 6:
                show_pump_performance();
                break;
            case 7:
                show_fuel_summary();
                break;
            case 8:
                show_hour_wise_analysis();
                break;
            case 9:
                show_payment_breakdown();
                break;
            case 10:
                print_sample_receipt_format();
                break;
            case 11:
                print_system_architecture();
                break;
            case 12:
                print_dynamic_allocation_advantages();
                break;
            case 0:
                printf("Exiting... freeing memory and shutting down.\n");
                shutdown_system();
                return 0;
            default:
                printf("Invalid choice.\n");
        }
    }

    /* unreachable, but keep for completeness */
    shutdown_system();
    return 0;
}