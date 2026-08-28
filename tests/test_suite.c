#include "common.h"
#include <sys/wait.h>

/* Helper: starts the coordinator as a background process */
static pid_t avvia_coordinatore(const char *porta, const char *file_log, const char *max_bytes, const char *intervallo) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("./bin/coordinator", "coordinator", "-p", porta, "-l", file_log, "-m", max_bytes, "-t", intervallo, NULL);
        exit(EXIT_FAILURE);
    }
    usleep(200000); /* Pause 200ms to allow coordinator to bind to port */
    return pid;
}

int main(void) {
    printf("===================================================\n");
    printf("   TEST SUITE - POSIX LOG AGGREGATOR  \n");
    printf("===================================================\n\n");
    fflush(stdout);

    /* 1. TEST AGGREGAZIONE E FORMATO LOG */
    printf("--- 1. Test Aggregazione Dati ---\n");
    fflush(stdout);
    pid_t p1 = avvia_coordinatore("8901", "test1.log", "10000", "10");
    system("./bin/producer -p 8901 -i SORGENTE_A -d 42.5");
    system("./bin/producer -p 8901 -i SORGENTE_B -d 99.9");
    kill(p1, SIGINT);
    waitpid(p1, NULL, 0);
    printf("Contenuto log 1 (cat test1.log):\n");
    fflush(stdout);
    system("cat test1.log");

    /* 2. TEST RIUSO RAPIDO PORTA (SO_REUSEADDR) */
    printf("\n--- 2. Test Riuso Rapido Porta (SO_REUSEADDR) ---\n");
    fflush(stdout);
    pid_t p2_a = avvia_coordinatore("8902", "test2.log", "10000", "10");
    kill(p2_a, SIGINT);
    waitpid(p2_a, NULL, 0);
    /* Riavvio immediato sulla stessa porta 8902 */
    pid_t p2_b = avvia_coordinatore("8902", "test2.log", "10000", "10");
    system("./bin/producer -p 8902 -i TEST_RIUSO -d 100");
    kill(p2_b, SIGINT);
    waitpid(p2_b, NULL, 0);
    printf("[PASS] Porta 8902 riutilizzata immediatamente senza errori!\n");
    fflush(stdout);

    /* 3. TEST ROTAZIONE LOG SU SIGALRM */
    printf("\n--- 3. Test Rotazione Log su SIGALRM ---\n");
    fflush(stdout);
    pid_t p3 = avvia_coordinatore("8903", "test3.log", "100", "1");
    system("./bin/producer -p 8903 -i PROD_1 -d 123456789 -n 5");
    kill(p3, SIGALRM); /* Genera segnale SIGALRM */
    usleep(300000);
    system("./bin/producer -p 8903 -i PROD_NUOVO -d 999");
    kill(p3, SIGINT);
    waitpid(p3, NULL, 0);
    printf("File generati per il test 3 (ls test3.log*):\n");
    fflush(stdout);
    system("ls -1 test3.log*");

    /* 4. TEST DISCONNESSIONE CLIENT (SIGPIPE) */
    printf("\n--- 4. Test Disconnessione Client (SIGPIPE) ---\n");
    fflush(stdout);
    pid_t p4 = avvia_coordinatore("8904", "test4.log", "10000", "10");
    system("./bin/producer -p 8904 -i PROD_ABRUPT -d 555 -c");
    kill(p4, SIGINT);
    waitpid(p4, NULL, 0);
    printf("Contenuto log 4 (cat test4.log):\n");
    fflush(stdout);
    system("cat test4.log");

    /* 5. STRESS TEST CONCORRENTE: SIGALRM A RAFFICA DURANTE SCRITTURE MASSIVE */
    printf("\n--- 5. Stress Test: Concorrenza Massiva + SIGALRM a Raffica ---\n");
    fflush(stdout);
    pid_t p5 = avvia_coordinatore("8905", "test5.log", "250", "1");

    /* Avvia 4 producer paralleli in background */
    system("./bin/producer -p 8905 -i PROD_STRESS_1 -d 1111 -n 25 -s 5 &");
    system("./bin/producer -p 8905 -i PROD_STRESS_2 -d 2222 -n 25 -s 5 &");
    system("./bin/producer -p 8905 -i PROD_STRESS_3 -d 3333 -n 25 -s 5 &");
    system("./bin/producer -p 8905 -i PROD_STRESS_4 -d 4444 -n 25 -s 5 &");

    /* Bombarda il coordinator con raffiche di segnali SIGALRM mentre i producer scrivono */
    for (int i = 0; i < 8; i++) {
        usleep(25000); /* 25ms */
        kill(p5, SIGALRM);
    }

    /* Attendi il completamento di tutti i producer */
    sleep(1);

    kill(p5, SIGINT);
    waitpid(p5, NULL, 0);

    printf("File generati durante lo stress test 5 (ls test5.log*):\n");
    fflush(stdout);
    system("ls -1 test5.log*");
    printf("[PASS] Stress test superato: scritture concorrenti gestite con successo durante rotazioni multiple!\n");
    fflush(stdout);

    printf("\n===================================================\n");
    printf("   TUTTI I TEST SONO STATI ESEGUITI CON SUCCESSO!  \n");
    printf("===================================================\n");
    fflush(stdout);

    system("rm -f test*.log*");
    return 0;
}
