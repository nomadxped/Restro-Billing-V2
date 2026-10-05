#include <mysql.h> //for mysql api
#include <stdio.h> //input and output
#include <stdlib.h>
#include <string.h> //handle string function
#include <time.h>   //For date and time for bill

#define MAX_BILL_ITEMS 100
#define MAX_BILLS 5 // how many bills can be open at the same time

struct BillItem {
  int id;         // item id
  char name[100]; // item name
  float price;    // item price
  int quantity;   // item quantity
  float total;    // total price
};

// one open bill (one customer)
struct Bill {
  int customer_id;
  char name[200];
  char phone[20];
  struct BillItem items[MAX_BILL_ITEMS];
  int item_count;
};

// Array system with 5 pointers to store up to 5 bills in primary memory
// NULL means the slot is currently free
struct Bill *bills[MAX_BILLS];

void space();
void admin_menu();
void user_menu();
int input();
void sales_report();
void display_menu();
char *now(void);
void menu_management();
void initialize_db();
int authenticate();

// billing functions
void billing_desk();                  // main menu for open bills
void list_open_bills();               // show all open bills
void select_customer(struct Bill *b); // register / anonymous
void edit_bill(int slot);             // add, edit, finalize, cancel, hold
void finalize_bill(struct Bill *b);   // save to database + receipt
void print_receipt(FILE *out, struct Bill *b, int invoice_id, float sub,
                   float pct, float disc, float grand);
void free_bill(int slot); // free memory of a slot

MYSQL *conn;
MYSQL_RES *result;
MYSQL_ROW row;

/*
======================================================================================================================
Main Function start
======================================================================================================================
*/
int main() {
  int choice, i, open_count = 0;

  conn = mysql_init(NULL);
  if (conn == NULL) {
    printf("mysql_init() failed\n");
    return 1;
  }

  if (mysql_real_connect(conn, "localhost", "root", "", NULL, 3306, NULL, 0) ==
      NULL) {
    printf("Connection failed: %s\n", mysql_error(conn));
    mysql_close(conn);
    return 1;
  }

  initialize_db();

  int role = authenticate();

  while (1) {
    printf("\n\tRestaurant Billing System\n");
    printf("-------------------------------------------------\n");
    if (role == 1) {
      admin_menu();
    } else if (role == 2) {
      user_menu();
    } else
      break;

    printf("[0] Exit / Logout\n");
    choice = input();

    if (choice == 0)
      break;

    switch (choice) {
    case 1: // billing desk (multiple open bills)
      billing_desk();
      break;
    case 4:
      if (role == 1) {
        menu_management();
      } else {
        printf("Access Denied!\n");
      }
      break;
    case 5:
      if (role == 1) {
        sales_report();
      } else {
        printf("Access Denied!\n");
      }
      break;
    default:
      printf("Not implemented or Invalid Choice.\n");
    }
  }

  // warn about unfinished bills, then free memory
  for (i = 0; i < MAX_BILLS; i++)
    if (bills[i] != NULL)
      open_count++;
  if (open_count > 0)
    printf("Warning: %d unfinished bill(s) discarded.\n", open_count);
  for (i = 0; i < MAX_BILLS; i++)
    free_bill(i);

  mysql_close(conn);
  return 0;
}
/*
======================================================================================================================
Main Function end
======================================================================================================================
*/

void initialize_db() {
  mysql_query(conn, "CREATE DATABASE IF NOT EXISTS testdb");
  mysql_select_db(conn, "testdb");

  mysql_query(conn, "CREATE TABLE IF NOT EXISTS menu ("
                    "item_id INT AUTO_INCREMENT PRIMARY KEY,"
                    "item_name VARCHAR(100) NOT NULL,"
                    "price FLOAT NOT NULL"
                    ")");

  mysql_query(conn, "CREATE TABLE IF NOT EXISTS customer ("
                    "customer_id INT AUTO_INCREMENT PRIMARY KEY,"
                    "customer_name VARCHAR(100),"
                    "phone VARCHAR(20)"
                    ")");

  mysql_query(conn, "CREATE TABLE IF NOT EXISTS invoice ("
                    "invoice_id INT AUTO_INCREMENT PRIMARY KEY,"
                    "customer_id INT,"
                    "invoice_date DATETIME,"
                    "grand_total FLOAT"
                    ")");

  mysql_query(conn, "CREATE TABLE IF NOT EXISTS invoice_items ("
                    "id INT AUTO_INCREMENT PRIMARY KEY,"
                    "invoice_id INT,"
                    "item_id INT,"
                    "quantity INT,"
                    "price FLOAT,"
                    "total FLOAT"
                    ")");
}

int authenticate() {
  printf("\tAuthenticate\n");
  char username[50];
  char password[50];
  printf("username:");
  scanf("%s", username);
  printf("password:");
  scanf("%s", password);

  if (!strcmp(username, "admin2083") && !strcmp(password, "adminpassword")) {
    space();
    printf("Logged in as Admin\n");
    return 1;
  } else if (!strcmp(username, "user2083") &&
             !strcmp(password, "userpassword")) {
    space();
    printf("Logged in as User\n");
    return 2;
  } else {
    space();
    printf("Wrong Credential!\n");
    return authenticate();
  }
}

void user_menu() {
  printf("[1] Billing Desk\n");
  printf("[2] View All Invoices\n");
  printf("[3] Search Invoice\n");
}

void admin_menu() {
  user_menu();
  printf("[4] Menu Management\n");
  printf("[5] Sales Report\n");
}

/* --------------------------------------------------------
   BILLING DESK (multiple open bills)
   -------------------------------------------------------- */
void billing_desk() {
  int ch, slot;

  while (1) {
    list_open_bills();
    printf("[1] New Bill\n[2] Open Existing Bill\n[0] Back\n");
    ch = input();

    if (ch == 0)
      break;

    if (ch == 1) {
      // Step 1: Find a free slot in the bills array
      slot = -1;
      int i;
      for (i = 0; i < MAX_BILLS; i++) {
        if (bills[i] == NULL) {
          slot = i; // Empty slot found
          break;
        }
      }

      // Step 2: Check if all 5 slots are full
      if (slot == -1) {
        printf("All %d bills are in use! Finish or cancel one first.\n",
               MAX_BILLS);
        continue;
      }

      // Step 3: Use DMA (malloc) to allocate primary memory for the new bill
      bills[slot] = (struct Bill *)malloc(sizeof(struct Bill));
      if (bills[slot] == NULL) {
        printf("Out of memory!\n"); // Memory allocation failed
        continue;
      }

      // Step 4: Initialize the item count to 0 since malloc DOES NOT zero out
      // memory
      bills[slot]->item_count = 0;

      // Step 5: Assign a customer to this dynamically allocated bill
      select_customer(bills[slot]);

      // Step 6: Proceed to edit the bill in the current slot
      edit_bill(slot);

    } else if (ch == 2) {
      printf("Enter bill number (1-%d): ", MAX_BILLS);
      slot = input() - 1;
      if (slot >= 0 && slot < MAX_BILLS && bills[slot] != NULL)
        edit_bill(slot);
      else
        printf("Invalid or empty bill number.\n");
    } else {
      printf("Invalid choice!\n");
    }
  }
}

void list_open_bills() {
  int i, j;
  printf("\n--- Open Bills ---\n");
  printf("%-5s %-20s %-8s %-10s\n", "No.", "Customer", "Items", "Total");
  printf("------------------------------------------\n");
  for (i = 0; i < MAX_BILLS; i++) {
    if (bills[i] == NULL) {
      printf("%-5d (free)\n", i + 1);
    } else {
      float sum = 0;
      for (j = 0; j < bills[i]->item_count; j++)
        sum += bills[i]->items[j].total;
      printf("%-5d %-20s %-8d %-10.2f\n", i + 1, bills[i]->name,
             bills[i]->item_count, sum);
    }
  }
  printf("------------------------------------------\n");
}

void select_customer(struct Bill *b) {
  char query[512];
  int choice;

  printf("\nEnter Customer Detail\n");
  printf("[1] Register\n[2] Anonymous\n");
  choice = input();

  if (choice == 1) {
    printf("Enter customer phone number: ");
    scanf("%19s", b->phone);
    fflush(stdin);

    sprintf(query,
            "SELECT customer_id, customer_name FROM customer WHERE phone='%s'",
            b->phone);
    mysql_query(conn, query);
    MYSQL_RES *cust_res = mysql_store_result(conn);
    MYSQL_ROW cust_row = mysql_fetch_row(cust_res);

    if (cust_row) {
      b->customer_id = atoi(cust_row[0]);
      strncpy(b->name, cust_row[1], sizeof(b->name) - 1);
      printf("Welcome back, %s!\n", b->name);
    } else {
      printf("Enter customer full name: ");
      gets(b->name);

      sprintf(query,
              "INSERT INTO customer(customer_name, phone) VALUES('%s','%s')",
              b->name, b->phone);
      mysql_query(conn, query);
      b->customer_id = (int)mysql_insert_id(conn);
    }
    mysql_free_result(cust_res);

  } else {
    b->customer_id = 0;
    strcpy(b->name, "Anonymous");
    strcpy(b->phone, "N/A");
  }
}

/* --------------------------------------------------------
   EDIT ONE BILL
   -------------------------------------------------------- */
void edit_bill(int slot) {
  struct Bill *b = bills[slot]; // pointer to this bill
  char query[512];
  int i;

  printf("\nMenu:\n");
  display_menu();

  while (1) {
    printf("\n--- Bill %d : %s ---\n", slot + 1, b->name);
    if (b->item_count == 0) {
      printf("Cart is empty.\n");
    } else {
      for (i = 0; i < b->item_count; i++) {
        printf("ID: %-5d | Name: %-20s | Price: %-10.2f | Qty: %-5d\n",
               b->items[i].id, b->items[i].name, b->items[i].price,
               b->items[i].quantity);
      }
    }
    printf("--------------------\n");

    int id;
    printf("\nEnter Item ID to Add\n[-1] Edit Cart\n[-2] Finish & Pay\n"
           "[-3] Cancel Bill\n[-4] Hold (back to Billing Desk)\n: ");
    scanf("%d", &id);

    if (id == -1) { // edit cart
      int edit_choice;
      printf("\n[1] Remove Item\n[2] Update Quantity\n[3] Cancel Edit\n: ");
      scanf("%d", &edit_choice);

      if (edit_choice == 1) {
        int remove_id, removed = 0;
        printf("Enter item ID to remove: ");
        scanf("%d", &remove_id);
        for (i = 0; i < b->item_count; i++) {
          if (b->items[i].id == remove_id) {
            int j;
            for (j = i; j < b->item_count - 1; j++)
              b->items[j] = b->items[j + 1];
            b->item_count--;
            removed = 1;
            puts("Item removed!");
            break;
          }
        }
        if (!removed)
          puts("Item not found in cart.");

      } else if (edit_choice == 2) {
        int update_id, new_qty, updated = 0;
        printf("Enter item ID to update: ");
        scanf("%d", &update_id);
        printf("Enter new quantity: ");
        scanf("%d", &new_qty);
        if (new_qty <= 0) {
          puts("Quantity must be greater than 0.");
          continue;
        }
        for (i = 0; i < b->item_count; i++) {
          if (b->items[i].id == update_id) {
            b->items[i].quantity = new_qty;
            b->items[i].total = new_qty * b->items[i].price;
            updated = 1;
            puts("Quantity updated!");
            break;
          }
        }
        if (!updated)
          puts("Item not found in cart.");
      }

    } else if (id == -2) { // finish
      if (b->item_count == 0) {
        printf("Cart empty, cannot finish.\n");
      } else {
        finalize_bill(b);
        free_bill(slot);
        return;
      }

    } else if (id == -3) { // cancel
      printf("Bill cancelled.\n");
      free_bill(slot);
      return;

    } else if (id == -4) { // hold, data stays in bills[slot]
      printf("Bill %d is on hold.\n", slot + 1);
      return;

    } else if (id > 0) { // add item
      if (b->item_count >= MAX_BILL_ITEMS) {
        printf("Cart full!\n");
        continue;
      }
      int qty;
      printf("Enter quantity: ");
      scanf("%d", &qty);
      if (qty <= 0) {
        puts("Quantity must be greater than 0.");
        continue;
      }

      sprintf(query, "SELECT item_name, price FROM menu WHERE item_id=%d", id);
      if (mysql_query(conn, query)) {
        printf("Query error: %s\n", mysql_error(conn));
        continue;
      }

      MYSQL_RES *item_res = mysql_store_result(conn);
      if (!item_res) {
        printf("Error fetching result\n");
        continue;
      }
      MYSQL_ROW item_row = mysql_fetch_row(item_res);
      if (!item_row) {
        puts("Invalid item ID!");
        mysql_free_result(item_res);
        continue;
      }

      struct BillItem *it = &b->items[b->item_count];
      it->id = id;
      strncpy(it->name, item_row[0], 99);
      it->name[99] = '\0';
      it->price = atof(item_row[1]);
      it->quantity = qty;
      it->total = qty * it->price;
      b->item_count++;

      mysql_free_result(item_res);
      puts("Item added!");

    } else {
      printf("Invalid input.\n");
    }
  }
}

/* --------------------------------------------------------
   FINALIZE: discount, save to database, receipt
   -------------------------------------------------------- */
void finalize_bill(struct Bill *b) {
  char query[512];
  int i;
  float discount_pct = 0;

  printf("Enter overall discount percentage (default 0%%): ");
  scanf("%f", &discount_pct);

  float sub_total = 0;
  for (i = 0; i < b->item_count; i++)
    sub_total += b->items[i].total;

  float discount_amt = sub_total * (discount_pct / 100.0);
  float grand_total = sub_total - discount_amt;

  // invoice header first to get invoice_id
  sprintf(query,
          "INSERT INTO invoice(customer_id, invoice_date, grand_total) "
          "VALUES(%d, NOW(), %.2f)",
          b->customer_id, grand_total);
  if (mysql_query(conn, query)) {
    fprintf(stderr, "Invoice insert failed: %s\n", mysql_error(conn));
    return;
  }
  int invoice_id = (int)mysql_insert_id(conn);

  // line items
  for (i = 0; i < b->item_count; i++) {
    sprintf(query,
            "INSERT INTO invoice_items(invoice_id, item_id, quantity, price, "
            "total) VALUES(%d, %d, %d, %.2f, %.2f)",
            invoice_id, b->items[i].id, b->items[i].quantity, b->items[i].price,
            b->items[i].total);
    if (mysql_query(conn, query))
      fprintf(stderr, "Line-item insert failed: %s\n", mysql_error(conn));
  }

  // print receipt on screen
  print_receipt(stdout, b, invoice_id, sub_total, discount_pct, discount_amt,
                grand_total);
  printf("Invoice #%d saved successfully.\n", invoice_id);

  // write receipt to file
  char filename[100];
  sprintf(filename, "invoice(%d).txt", invoice_id);
  FILE *fp = fopen(filename, "w");
  if (fp) {
    print_receipt(fp, b, invoice_id, sub_total, discount_pct, discount_amt,
                  grand_total);
    fclose(fp);
    printf("Receipt written to %s.\n", filename);
  } else {
    printf("Error: Could not create %s\n", filename);
  }
}

// same function prints to screen (stdout) or to a file
void print_receipt(FILE *out, struct Bill *b, int invoice_id, float sub,
                   float pct, float disc, float grand) {
  int i;
  fprintf(out, "\n========== INVOICE ==========\n");
  fprintf(out, "Invoice #: %d\n", invoice_id);
  fprintf(out, "Date     : %s\n", now());
  fprintf(out, "Customer : %s\n", b->name);
  fprintf(out, "Phone    : %s\n\n", b->phone);
  fprintf(out, "%-5s %-20s %-10s %-6s %-10s\n", "ID", "Item", "Price", "Qty",
          "Total");
  fprintf(out, "----------------------------------------------------------\n");
  for (i = 0; i < b->item_count; i++) {
    fprintf(out, "%-5d %-20s %-10.2f %-6d %-10.2f\n", b->items[i].id,
            b->items[i].name, b->items[i].price, b->items[i].quantity,
            b->items[i].total);
  }
  fprintf(out, "----------------------------------------------------------\n");
  fprintf(out, "Sub-Total       : %.2f\n", sub);
  fprintf(out, "Overall Discount: %.1f%%\n", pct);
  fprintf(out, "Discount Amount : %.2f\n", disc);
  fprintf(out, "Grand Total     : %.2f\n", grand);
  fprintf(out, "=============================\n");
}

// Free dynamically allocated memory of one bill slot and mark it as free
void free_bill(int slot) {
  if (bills[slot] != NULL) {
    // Step 1: Free the memory allocated by malloc
    free(bills[slot]);
    // Step 2: Set pointer back to NULL to indicate slot is ready for next bill
    bills[slot] = NULL;
  }
}

/* --------------------------------------------------------
   SALES REPORT
   -------------------------------------------------------- */
void sales_report(void) {
  printf("\n========== SALES REPORT ==========\n");

  mysql_query(conn, "SELECT COUNT(*) AS invoices, SUM(grand_total) AS revenue "
                    "FROM invoice");
  MYSQL_RES *r = mysql_store_result(conn);
  MYSQL_ROW row = mysql_fetch_row(r);
  printf("Total invoices : %s\n", row[0]);
  printf("Total revenue  : %.2f\n", row[1] ? atof(row[1]) : 0.0);
  mysql_free_result(r);

  printf("\nDaily Sales (last 7 days):\n");
  printf("%-15s  %-10s  %-12s\n", "Date", "Invoices", "Revenue");
  printf("---------------------------------------\n");

  mysql_query(conn, "SELECT DATE(invoice_date) AS day, "
                    "       COUNT(*)           AS cnt, "
                    "       SUM(grand_total)   AS rev "
                    "FROM invoice "
                    "WHERE invoice_date >= CURDATE() - INTERVAL 7 DAY "
                    "GROUP BY day "
                    "ORDER BY day DESC");

  r = mysql_store_result(conn);
  while ((row = mysql_fetch_row(r)) != NULL)
    printf("%-15s  %-10s  %-12.2f\n", row[0], row[1], atof(row[2]));
  mysql_free_result(r);

  printf("\nTop 5 Best-Selling Items:\n");
  printf("%-5s %-20s %-10s %-12s\n", "ID", "Item", "Qty Sold", "Revenue");
  printf("-----------------------------------------------\n");

  mysql_query(conn, "SELECT m.item_id, m.item_name, "
                    "       SUM(ii.quantity)    AS total_qty, "
                    "       SUM(ii.total)       AS total_rev "
                    "FROM invoice_items ii "
                    "JOIN menu m ON m.item_id = ii.item_id "
                    "GROUP BY m.item_id, m.item_name "
                    "ORDER BY total_qty DESC "
                    "LIMIT 5");

  r = mysql_store_result(conn);
  while ((row = mysql_fetch_row(r)) != NULL)
    printf("%-5s %-20s %-10s %-12.2f\n", row[0], row[1], row[2], atof(row[3]));
  mysql_free_result(r);

  printf("===================================\n");
}

void display_menu() {
  if (mysql_query(conn, "SELECT * FROM menu")) {
    fprintf(stderr, "Query failed: %s\n", mysql_error(conn));
    return;
  }

  result = mysql_store_result(conn);
  if (result == NULL) {
    fprintf(stderr, "mysql_store_result() failed: %s\n", mysql_error(conn));
    return;
  }

  int num_fields = mysql_num_fields(result);
  MYSQL_FIELD *fields = mysql_fetch_fields(result);

  printf("\n");
  int i;
  for (i = 0; i < num_fields; i++)
    printf("%-35s", fields[i].name);
  printf("\n");
  for (i = 0; i < num_fields; i++)
    printf("--------------------------------------");
  printf("\n");

  while ((row = mysql_fetch_row(result)) != NULL) {
    for (i = 0; i < num_fields; i++)
      printf("%-35s", row[i] ? row[i] : "NULL");
    printf("\n");
  }
  mysql_free_result(result);
}

/* --------------------------------------------------------
   MENU MANAGEMENT (CRUD)
   -------------------------------------------------------- */
void menu_management() {
  int choice;
  char query[256];

  while (1) {
    printf("\n--- Menu Management ---\n");
    printf("[1] Add Item\n");
    printf("[2] Edit Item\n");
    printf("[3] Delete Item\n");
    printf("[4] View All Items\n");
    printf("[5] Go Back\n");
    choice = input();

    if (choice == 1) {
      char name[100];
      float price;

      fflush(stdin);
      printf("Enter item name: ");
      gets(name);

      printf("Enter item price: ");
      scanf("%f", &price);

      sprintf(query, "INSERT INTO menu(item_name, price) VALUES('%s', %.2f)",
              name, price);
      if (mysql_query(conn, query))
        printf("Error adding item: %s\n", mysql_error(conn));
      else
        printf("Item added successfully!\n");

    } else if (choice == 2) {
      int id;
      float new_price;

      printf("Enter item ID to edit: ");
      scanf("%d", &id);
      printf("Enter new price: ");
      scanf("%f", &new_price);

      sprintf(query, "UPDATE menu SET price=%.2f WHERE item_id=%d", new_price,
              id);
      if (mysql_query(conn, query))
        printf("Error updating item: %s\n", mysql_error(conn));
      else
        printf("Item updated successfully!\n");

    } else if (choice == 3) {
      int id;

      printf("Enter item ID to delete: ");
      scanf("%d", &id);

      sprintf(query, "DELETE FROM menu WHERE item_id=%d", id);
      if (mysql_query(conn, query))
        printf("Error deleting item: %s\n", mysql_error(conn));
      else
        printf("Item deleted successfully!\n");

    } else if (choice == 4) {
      printf("\n--- Menu Items ---\n");
      display_menu();
    } else if (choice == 5) {
      break;
    } else {
      printf("Invalid choice!\n");
    }
  }
}

/* --------------------------------------------------------
   HELPERS
   -------------------------------------------------------- */
void space() {
  int i;
  for (i = 1; i < 50; i++)
    printf("\n");
}

int input() {
  int choice;
  printf("Select Option:");
  scanf("%d", &choice);
  return choice;
}

char *now(void) {
  static char current_time[100];
  time_t mytime;
  time(&mytime);
  strftime(current_time, 100, "%Y/%m/%d %H:%M:%S", localtime(&mytime));
  return current_time;
}

/*
gcc main.c -o main.exe -I"C:\Program Files\MySQL\MySQL Server 8.0\include"
-L"C:\Program Files\MySQL\MySQL Server 8.0\lib" -lmysql
*/
