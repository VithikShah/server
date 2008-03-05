#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <db.h>
#if defined(OSX)
    #include <sys/syscall.h>
#else
    #include <syscall.h>
#endif
#include <pthread.h>
#include "test.h"

static inline unsigned int getmyid() {
#if __linux__
    return syscall(__NR_gettid);
#else
    return getpid();
#endif
}

typedef unsigned int my_t;

struct db_inserter {
    pthread_t tid;
    DB *db;
    my_t startno, endno;
    int do_exit;
};

int db_put(DB *db, my_t k, my_t v) {
    DBT key, val;
    int r = db->put(db, 0, dbt_init(&key, &k, sizeof k), dbt_init(&val, &v, sizeof v), DB_YESOVERWRITE);
    return r;
}

void *do_inserts(void *arg) {
    struct db_inserter *mywork = (struct db_inserter *) arg;
    if (verbose) printf("%lu:%u:do_inserts:start:%u-%u\n", (unsigned long)pthread_self(), getmyid(), mywork->startno, mywork->endno);
    my_t i;
    for (i=mywork->startno; i < mywork->endno; i++) {
        int r = db_put(mywork->db, htonl(i), i); assert(r == 0);
    }
    
    if (verbose) printf("%lu:%u:do_inserts:end\n", (unsigned long)pthread_self(), getmyid());
    if (mywork->do_exit) pthread_exit(arg);
    return 0;
}

int usage() {
    fprintf(stderr, "test OPTIONS\n");
    fprintf(stderr, "[-n NTUPLES] (default:1000000)\n");
    fprintf(stderr, "[-p NTHREADS] (default:1)\n");
    fprintf(stderr, "[-a] all work on threads (default:0)\n");
    fprintf(stderr, "[-thread_stack N] (overrides the default whatever it is\n");
    fprintf(stderr, "[-resume] resume writing to a db\n");
    return 1;
}

int main(int argc, char *argv[]) {
    const char *dbfile = "test.db";
    const char *dbname = "main";
    int all_on_threads = 0;
    int nthreads = 1;
    my_t n = 1000000;
    int thread_stack = 0;
    int do_resume = 0;

    int i;
    for (i=1; i<argc; i++) {
        char *arg = argv[i];
        if (0 == strcmp(arg, "-h") || 0 == strcmp(arg, "--help")) {
            return usage();
        }
        if (0 == strcmp(arg, "-v") || 0 == strcmp(arg, "--verbose")) {
            verbose = 1; 
            continue;
        }
        if (0 == strcmp(arg, "-p")) {
            if (i+1 >= argc) return usage();
            nthreads = atoi(argv[++i]);
            continue;
        }
        if (0 == strcmp(arg, "-n")) {
            if (i+1 >= argc) return usage();
            n = atoi(argv[++i]);
            continue;
        }
        if (0 == strcmp(arg, "-a")) {
            all_on_threads = 1;
            continue;
        }
        if (0 == strcmp(arg, "-thread_stack") || 0 == strcmp(arg, "--thread_stack")) {
            if (i+1 >= argc) return usage();
            thread_stack = atoi(argv[++i]);
            continue;
        }
        if (0 == strcmp(arg, "-resume")) {
            do_resume = 1;
            continue;
        }
    }

    if (!do_resume) {
        system("rm -rf " DIR);
        mkdir(DIR, 0777);
    }

    int r;
    DB_ENV *env;

    r = db_env_create(&env, 0); assert(r == 0);
    r = env->set_cachesize(env, 0, 8000000, 1); assert(r == 0);
    r = env->open(env, DIR, DB_CREATE + DB_THREAD + DB_PRIVATE + DB_INIT_MPOOL + DB_INIT_LOCK, 0777); assert(r == 0);

    DB *db;

    r = db_create(&db, env, 0); assert(r == 0);
    r = db->open(db, 0, dbfile, dbname, DB_BTREE, DB_CREATE + DB_THREAD, 0777); assert(r == 0);

    struct db_inserter work[nthreads];

    for (i=0; i<nthreads; i++) {
        work[i].db = db;
        work[i].startno = i*(n/nthreads);
        work[i].endno = work[i].startno + (n/nthreads);
        work[i].do_exit =1 ;
        if (i+1 == nthreads)  
            work[i].endno = n;
    }

    if (verbose) printf("pid:%u\n", getpid());

    for (i=all_on_threads ? 0 : 1; i<nthreads; i++) {
        pthread_attr_t attr;
        r = pthread_attr_init(&attr); assert(r == 0);
        if (thread_stack) {
            r = pthread_attr_setstacksize(&attr, thread_stack); assert(r == 0);
        }
        r = pthread_create(&work[i].tid, &attr, do_inserts, &work[i]); assert(r == 0);
    }

    if (!all_on_threads) {
        work[0].do_exit = 0;
        do_inserts(&work[0]);
    }

    for (i=all_on_threads ? 0 : 1; i<nthreads; i++) {
        void *ret;
        r = pthread_join(work[i].tid, &ret); assert(r == 0);
    }

    r = db->close(db, 0); assert(r == 0);
    r = env->close(env, 0); assert(r == 0);

    return 0;
}
