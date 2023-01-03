#include <bits/stdc++.h>
#include <pthread.h>

#include "erikmtalloc.h"

using namespace std;

#define EXPECT_PASS(x) { if (!(x)) { cout << __FUNCTION__ << " failed on line " << __LINE__ << endl; } }
#define EXPECT_FAIL(x) { if ((x)) { cout << __FUNCTION__ << " failed on line " << __LINE__ << endl; } }
#define LARGE_CAPACITY 65535

struct TestStruct {
    char a[1024];
    char b[512];
    char c[256];
    char d[47];
    int e[10];
    double f[42];

    void toString() {
        cout << a[0] << " " << b[0] << c[0] << d[0] << e[0] << f[0] << endl;
    }
};

struct LargeTestStruct {
    char g[LARGE_CAPACITY];
};

// Simple test
bool simple_new_test() {
    cout << "RUNNING simple_new_test" << endl;

    int *a = new int[256];

    *a = 42;

    assert(*a == 42);
    cout << "TEST VALUE: " << *a << endl;

    delete[] a;

    return true;
}

bool simple_new_test2() {
    cout << "RUNNING simple_new_test2" << endl;

    int *a = new int;

    *a = 6;

    assert(*a == 6);
    cout << "TEST VALUE: " << *a << endl;

    delete a;

    return true;
}

bool simple_new_test3() {
    TestStruct *ts = new TestStruct;

    for (int i=0; i<sizeof(ts->a)/sizeof(char); ++i) ts->a[i] = 'X';
    for (int i=0; i<sizeof(ts->b)/sizeof(char); ++i) ts->b[i] = 'X';
    for (int i=0; i<sizeof(ts->c)/sizeof(char); ++i) ts->c[i] = 'X';
    for (int i=0; i<sizeof(ts->d)/sizeof(char); ++i) ts->d[i] = 'X';
    for (int i=0; i<sizeof(ts->e)/sizeof(int); ++i) ts->e[i] = 7;
    for (int i=0; i<sizeof(ts->f)/sizeof(double); ++i) ts->f[i] = 3.14;

    assert(ts->a[0] == 'X');
    assert(ts->b[0] == 'X');
    assert(ts->c[0] == 'X');
    assert(ts->d[0] == 'X');
    assert(ts->e[0] == 7);
    assert(ts->f[0] == 3.14);

    delete ts;

    return true;
}

bool simple_new_test4() {
    LargeTestStruct *ts = new LargeTestStruct;

    //for (int i=0; i<sizeof(ts->g); ++i) ts->g[i] = 'A';

    delete ts;

    return true;
}

bool simple_new_test5() {
    LargeTestStruct *ts = new LargeTestStruct;
    LargeTestStruct *ts2 = new LargeTestStruct;
    LargeTestStruct *ts3 = new LargeTestStruct;
    LargeTestStruct *ts4 = new LargeTestStruct;
    LargeTestStruct *ts5 = new LargeTestStruct;

    for (int i=0; i<sizeof(ts->g); ++i) ts->g[i] = 'A';
    for (int i=0; i<sizeof(ts2->g); ++i) ts2->g[i] = 'A';
    for (int i=0; i<sizeof(ts3->g); ++i) ts3->g[i] = 'A';
    for (int i=0; i<sizeof(ts4->g); ++i) ts4->g[i] = 'A';
    for (int i=0; i<sizeof(ts5->g); ++i) ts5->g[i] = 'A';

    assert(ts->g[0] == 'A');
    assert(ts2->g[LARGE_CAPACITY-1] == 'A');
    assert(ts3->g[0] == 'A');
    assert(ts4->g[0] == 'A');
    assert(ts5->g[0] == 'A');

    delete ts;
    delete ts2;
    delete ts3;
    delete ts4;
    delete ts5;

    return true;
}

// Goals
// - Test base case (no modifications to allocator) for time per allocation
// - Test with modified allocator and measure delta
// - Test n threads concurrently to test for locking functionality or side effects
void *thread_test1(void *threadid) {
    long tid;
    tid = (long) threadid;

    cout << "Starting thread: " << tid << endl;

    TestStruct *ts = new TestStruct;

    for (int i=0; i<sizeof(ts->a)/sizeof(char); ++i) ts->a[i] = 'X';
    for (int i=0; i<sizeof(ts->b)/sizeof(char); ++i) ts->b[i] = 'X';
    for (int i=0; i<sizeof(ts->c)/sizeof(char); ++i) ts->c[i] = 'X';
    for (int i=0; i<sizeof(ts->d)/sizeof(char); ++i) ts->d[i] = 'X';
    for (int i=0; i<sizeof(ts->e)/sizeof(int); ++i) ts->e[i] = 7;
    for (int i=0; i<sizeof(ts->f)/sizeof(double); ++i) ts->f[i] = 3.14;

    delete ts;
}

void thread_runner(int numthreads) {
    pthread_t threads[numthreads];

    auto start = std::chrono::system_clock::now();

    for (long i=0; i < numthreads; ++i) {
        int rc = pthread_create(&threads[i], NULL, thread_test1, (void *) i);

        if (rc) cout << "Thread " << i << " failed to execute." << endl;
    }

    for (long i=0; i < numthreads; ++i) pthread_join(threads[i], NULL);

    auto end = std::chrono::system_clock::now();
    chrono::duration<double> elapsed_time = end - start;
    cout << "ELAPSED TIME: " << elapsed_time.count() << " seconds" << endl;
}

void run_simple_new_tests() {
    EXPECT_PASS(simple_new_test());

    EXPECT_PASS(simple_new_test2());
    EXPECT_PASS(simple_new_test2());
    EXPECT_PASS(simple_new_test2());
    EXPECT_PASS(simple_new_test2());

    EXPECT_PASS(simple_new_test3());

    EXPECT_PASS(simple_new_test4());
    EXPECT_PASS(simple_new_test4());
    EXPECT_PASS(simple_new_test4());
    EXPECT_PASS(simple_new_test4());
    EXPECT_PASS(simple_new_test4());

    EXPECT_PASS(simple_new_test3());
    EXPECT_PASS(simple_new_test3());
    EXPECT_PASS(simple_new_test3());
    EXPECT_PASS(simple_new_test3());

    EXPECT_PASS(simple_new_test4());
    EXPECT_PASS(simple_new_test4());
    EXPECT_PASS(simple_new_test4());

    EXPECT_PASS(simple_new_test5());
    EXPECT_PASS(simple_new_test5());
    EXPECT_PASS(simple_new_test5());
    EXPECT_PASS(simple_new_test5());
    
    // Thread tests
    thread_runner(512);
}

// Timed loop of new / delete
// Timed multithreaded loop of new / delete
// Cause thread corruption with locking disabled?


int main() {
    // Note to self: sync_with_stdio calls new to allocate buffers
    ios_base::sync_with_stdio(1);
    cout.tie(0);

    cout << "Starting.." << endl;

    run_simple_new_tests();
    print_memory_stack();
}