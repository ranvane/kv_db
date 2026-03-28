#include "kv_store.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void test_basic_crud() {
    printf("Testing Basic CRUD...\n");
    kv_db_t *db = kv_open("./test_db");
    assert(db != NULL);

    kv_value_t val;
    val.type = KV_TYPE_INT;
    val.value.i = 12345;
    assert(kv_set(db, "key1", val));

    kv_value_t get_val;
    assert(kv_get(db, "key1", &get_val));
    assert(get_val.type == KV_TYPE_INT);
    assert(get_val.value.i == 12345);
    printf("  Set/Get Int: OK\n");

    val.type = KV_TYPE_STRING;
    val.value.s = "hello world";
    val.length = strlen(val.value.s);
    assert(kv_set(db, "key2", val));

    assert(kv_get(db, "key2", &get_val));
    assert(get_val.type == KV_TYPE_STRING);
    assert(strcmp(get_val.value.s, "hello world") == 0);
    kv_value_free(&get_val);
    printf("  Set/Get String: OK\n");

    assert(kv_delete(db, "key1"));
    assert(!kv_get(db, "key1", &get_val));
    printf("  Delete: OK\n");

    kv_close(db);
    printf("Basic CRUD: OK\n\n");
}

void test_persistence() {
    printf("Testing Persistence...\n");
    kv_db_t *db = kv_open("./test_db");
    kv_value_t val;
    val.type = KV_TYPE_INT;
    val.value.i = 999;
    kv_set(db, "persist_key", val);
    kv_close(db);

    // Reopen and check
    db = kv_open("./test_db");
    kv_value_t get_val;
    assert(kv_get(db, "persist_key", &get_val));
    assert(get_val.value.i == 999);
    kv_close(db);
    printf("Persistence: OK\n\n");
}

void test_transactions() {
    printf("Testing Transactions...\n");
    kv_db_t *db = kv_open("./test_db");
    
    kv_begin(db);
    kv_value_t val;
    val.type = KV_TYPE_INT;
    val.value.i = 1;
    kv_set(db, "txn_key1", val);
    val.value.i = 2;
    kv_set(db, "txn_key2", val);
    
    // Not committed yet, should not be in DB (except in txn log)
    kv_value_t get_val;
    assert(!kv_get(db, "txn_key1", &get_val));
    
    kv_commit(db);
    assert(kv_get(db, "txn_key1", &get_val));
    assert(get_val.value.i == 1);
    
    kv_begin(db);
    val.value.i = 3;
    kv_set(db, "txn_key1", val);
    kv_rollback(db);
    
    assert(kv_get(db, "txn_key1", &get_val));
    assert(get_val.value.i == 1); // Should still be 1
    
    kv_close(db);
    printf("Transactions: OK\n\n");
}

int main() {
#ifdef _WIN32
    system("if exist test_db rd /s /q test_db");
#else
    system("rm -rf ./test_db");
#endif
    test_basic_crud();
    test_persistence();
    test_transactions();
    printf("All C Core Tests Passed!\n");
    return 0;
}
