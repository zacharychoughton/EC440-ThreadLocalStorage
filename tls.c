#include "tls.h"
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#define HASH 23

typedef struct ThreadLocalStorage {
    unsigned int size;
    unsigned int page_num;
    struct Page** pages;
} ThreadLocalStorage;

typedef struct Page {
    unsigned long int address;
    int ref_count;
} Page;

typedef struct Item {
    pthread_t tid;
    ThreadLocalStorage* tls;
    struct Item* next;
} Item;

unsigned long int page_size = 0;
pthread_mutex_t mutex;
Item* Table[HASH];

// Aux Functions 
static int hash_calc(pthread_t tid) {
    return (unsigned int)((unsigned long)tid) % HASH;
}

static Item* find_item(pthread_t tid) {
    int i = hash_calc(tid);

    if (Table[i] != NULL) {
        Item* element = Table[i];
        while (element != NULL) {
            if (element->tid == tid) {
                return element;
            }
            element = element->next;
        }
    }
    return NULL;
}

static bool add_item(Item* new) {
    pthread_t tid = pthread_self();
    int index = hash_calc(tid);

    if (Table[index] != NULL) {
        Item* element = Table[index];
        Table[index] = new;
        Table[index]->next = element;
        return true;
    } else {
        Table[index] = new;
        Table[index]->next = NULL;
        return true;
    }

    return false;
}

static Item* remove_item(pthread_t tid) {
    int index = hash_calc(tid);

    if (Table[index] != NULL) {
        Item* temp = NULL;
        Item* current = Table[index];

        while (current != NULL) {
            if (current->tid == tid) {
                break;
            }
            temp = current;
            current = current->next;
        }
        if (temp != NULL) {
            temp->next = current->next;
        }

        return current;
    } else {
        return NULL;
    }
}

static void tls_handle_page_fault(int sig, siginfo_t *si, void *context) {
    unsigned long int p_fault = ((unsigned long int) si->si_addr & ~(page_size - 1));

    for (int i = 0; i < HASH; i++) {
        if (Table[i]) {
            Item* element = Table[i];

            while (element) {
                for (int j = 0; j < element->tls->page_num; j++) {
                    Page* page = element->tls->pages[j];

                    if (page->address == p_fault) {
                        pthread_exit(NULL);
                    }
                }
                element = element->next;
            }
        }
    }

    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    raise(sig);
}

static void tls_init() {
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = SA_SIGINFO;
    sigact.sa_sigaction = tls_handle_page_fault;
    sigaction(SIGBUS, &sigact, NULL);
    sigaction(SIGSEGV, &sigact, NULL);

    page_size = getpagesize();
    pthread_mutex_init(&mutex, NULL);
}

static void tls_protect(Page* p) {
    if (mprotect((void *)p->address, page_size, PROT_NONE)) {
        fprintf(stderr, "tls_protect: could not protect page\n");
        exit(1);
    }
}

static void tls_unprotect(Page* p, const int protect) {
    if (mprotect((void *)p->address, page_size, protect)) {
        fprintf(stderr, "tls_unprotect: could not unprotect page\n");
        exit(1);
    }
}


// tls_Functions: 
int tls_create(unsigned int size) {
    static bool is_first_call = true;

    if (is_first_call) {
        tls_init();
        is_first_call = false;
    }

    pthread_t TID = pthread_self();

    if (size <= 0 || find_item(TID) != NULL) {
        return -1;
    }

    ThreadLocalStorage* TLS = (ThreadLocalStorage*)malloc(sizeof(ThreadLocalStorage));
    TLS->size = size;
    TLS->page_num = size / page_size + (size % page_size != 0);

    TLS->pages = (Page**)calloc(TLS->page_num, sizeof(Page*));
    for (int i = 0; i < TLS->page_num; i++) {
        Page* page = (Page*)malloc(sizeof(Page));
        page->address = (unsigned long int)mmap(0, page_size, PROT_NONE, (MAP_ANON | MAP_PRIVATE), 0, 0);

        if (page->address == (unsigned long int)MAP_FAILED) {
            return -1;
        }

        page->ref_count = 1;
        TLS->pages[i] = page;
    }

    Item* element = (Item*)malloc(sizeof(Item));
    element->tls = TLS;
    element->tid = TID;

    if (add_item(element)) {
        return 0;
    } else {
        return -1;
    }
}

int tls_destroy() {
    pthread_t TID = pthread_self();
    Item* element = remove_item(TID);

    if (element == NULL) {
        return -1;
    }

    for (int i = 0; i < element->tls->page_num; i++) {
        Page* page = element->tls->pages[i];
        if (element->tls->pages[i]->ref_count == 1) {
            munmap((void *)page->address, page_size);
            free(page);
        } else {
            (element->tls->pages[i]->ref_count)--;
        }
    }
    free(element->tls->pages);
    free(element->tls);
    free(element);

    return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer) {
    pthread_t TID = pthread_self();
    Item* element = find_item(TID);

    if (element == NULL) {
        return -1;
    }

    ThreadLocalStorage* TLS = element->tls;

    if ((offset + length) > TLS->size) {
        return -1;
    }

    pthread_mutex_lock(&mutex);

    for (int i = 0; i < TLS->page_num; i++) {
        tls_unprotect(TLS->pages[i], PROT_READ);
    }

    for (int count = 0, index = offset; index < (offset + length); ++count, ++index) {
        Page* page;
        unsigned int page_number = index / page_size;
        unsigned int page_offset = index % page_size;
        page = TLS->pages[page_number];

        char *page_byte = (char *)(page->address + page_offset);
        buffer[count] = *page_byte;
    }

    for (int i = 0; i < TLS->page_num; i++) {
        tls_protect(TLS->pages[i]);
    }

    pthread_mutex_unlock(&mutex);

    return 0;
}

int tls_write(unsigned int offset, unsigned int length, const char *buffer) {
    pthread_t TID = pthread_self();
    Item* element = find_item(TID);

    if (element == NULL) {
        return -1;
    }

    ThreadLocalStorage* TLS = element->tls;

    if ((offset + length) > TLS->size) {
        return -1;
    }

    pthread_mutex_lock(&mutex);

    for (int i = 0; i < TLS->page_num; i++) {
        tls_unprotect(TLS->pages[i], PROT_WRITE);
    }

    for (int count = 0, index = offset; index < (offset + length); ++count, ++index) {
        Page *page, *page_copy;
        unsigned int page_number = index / page_size;
        unsigned int page_offset = index % page_size;
        page = TLS->pages[page_number];

        if (page->ref_count > 1) {
            page_copy = (Page *)malloc(sizeof(Page));
            page_copy->address = (unsigned long int)mmap(0, page_size, PROT_WRITE, (MAP_ANON | MAP_PRIVATE), 0, 0);
            memcpy((void *)page_copy->address, (void *)page->address, page_size);
            page_copy->ref_count = 1;
            TLS->pages[page_number] = page_copy;

            (page->ref_count)--;
            tls_protect(page);

            page = page_copy;
        }

        char *page_byte = (char *)(page->address + page_offset);
        *page_byte = buffer[count];
    }

    for (int i = 0; i < TLS->page_num; i++) {
        tls_protect(TLS->pages[i]);
    }

    pthread_mutex_unlock(&mutex);

    return 0;
}

int tls_clone(pthread_t tid) {
    pthread_t clone_tid = pthread_self();
    Item* clone_element = find_item(clone_tid);
    Item* target_element = find_item(tid);

    if (clone_element != NULL || target_element == NULL) {
        return -1;
    }

    ThreadLocalStorage* targetTLS = target_element->tls;

    clone_element = (Item *)malloc(sizeof(Item));
    clone_element->tid = clone_tid;

    clone_element->tls = (ThreadLocalStorage *)malloc(sizeof(ThreadLocalStorage));
    clone_element->tls->page_num = targetTLS->page_num;
    clone_element->tls->size = targetTLS->size;

    clone_element->tls->pages = (Page **)calloc(clone_element->tls->page_num, sizeof(Page *));

    for (int i = 0; i < clone_element->tls->page_num; i++) {
        clone_element->tls->pages[i] = targetTLS->pages[i];
        (clone_element->tls->pages[i]->ref_count)++;
    }

    if (add_item(clone_element)) {
        return 0;
    } else {
        return -1;
    }
}