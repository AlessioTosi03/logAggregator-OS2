#include "common.h"
#include <sys/wait.h>
#include <glob.h>

static int g_passed = 0;
static int g_failed = 0;

/* Macro: valuta una condizione e registra PASS/FAIL */
#define CHECK(cond, msg) do { \
    if (cond) { printf("    [PASS] %s\n", msg); g_passed++; } \
    else      { printf("    [FAIL] %s\n", msg); g_failed++; } \
} while (0)

/* Helper: avvia il coordinatore come processo figlio in background */
static pid_t avvia_coordinatore(const char *porta, const char *file_log, const char *max_bytes, const char *intervallo) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("./bin/coordinator", "coordinator", "-p", porta, "-l", file_log, "-m", max_bytes, "-t", intervallo, NULL);
        exit(EXIT_FAILURE);
    }
    usleep(200000); /* Pausa 200ms per consentire il bind sulla porta */
    return pid;
}

/* Helper: verifica se un file contiene una determinata sottostringa */
static int file_contains(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) { found = 1; break; }
    }
    fclose(f);
    return found;
}

/* Helper: conta quante righe del file contengono la sottostringa */
static int count_lines_with(const char *path, const char *needle) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[512];
    int n = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, needle)) n++;
    }
    fclose(f);
    return n;
}

/* Helper: conta i file che corrispondono a un pattern glob */
static int count_glob(const char *pattern) {
    glob_t g;
    int n = 0;
    if (glob(pattern, 0, NULL, &g) == 0) {
        n = (int)g.gl_pathc;
        globfree(&g);
    }
    return n;
}

/* Helper: verifica se ALMENO UN file che corrisponde al pattern contiene la sottostringa */
static int any_glob_contains(const char *pattern, const char *needle) {
    glob_t g;
    if (glob(pattern, 0, NULL, &g) != 0) return 0;
    int found = 0;
    for (size_t i = 0; i < g.gl_pathc && !found; i++) {
        if (file_contains(g.gl_pathv[i], needle)) found = 1;
    }
    globfree(&g);
    return found;
}

int main(void) {
    printf("===================================================\n");
    printf("   TEST SUITE - POSIX LOG AGGREGATOR  \n");
    printf("===================================================\n\n");
    fflush(stdout);

    /* 1. TEST AGGREGAZIONE E FORMATO LOG */
    printf("--- 1. Test Aggregazione Dati e Formato ---\n");
    fflush(stdout);
    pid_t p1 = avvia_coordinatore("8901", "test1.log", "10000", "10");
    system("./bin/producer -p 8901 -i SORGENTE_A -d 42.5");
    system("./bin/producer -p 8901 -i SORGENTE_B -d 99.9");
    usleep(300000); /* Attendi che il coordinatore processi i messaggi ricevuti */
    kill(p1, SIGINT);
    waitpid(p1, NULL, 0);
    CHECK(file_contains("test1.log", "SORGENTE_A"), "Log contiene dati di SORGENTE_A");
    CHECK(file_contains("test1.log", "SORGENTE_B"), "Log contiene dati di SORGENTE_B");
    CHECK(file_contains("test1.log", "42.5"), "Log contiene il dato numerico 42.5");
    CHECK(file_contains("test1.log", "99.9"), "Log contiene il dato numerico 99.9");
    CHECK(file_contains("test1.log", "[") && file_contains("test1.log", "]"),
          "Formato riga [TIMESTAMP, ID, DATO] presente");

    /* 2. TEST RIUSO RAPIDO PORTA (SO_REUSEADDR) */
    printf("\n--- 2. Test Riuso Rapido Porta (SO_REUSEADDR) ---\n");
    fflush(stdout);
    pid_t p2_a = avvia_coordinatore("8902", "test2.log", "10000", "10");
    kill(p2_a, SIGINT);
    waitpid(p2_a, NULL, 0);
    /* Riavvio immediato sulla stessa porta 8902 */
    pid_t p2_b = avvia_coordinatore("8902", "test2.log", "10000", "10");
    int rc2 = system("./bin/producer -p 8902 -i TEST_RIUSO -d 100 > /dev/null 2>&1");
    usleep(300000); /* Attendi che il coordinatore processi il messaggio */
    kill(p2_b, SIGINT);
    waitpid(p2_b, NULL, 0);
    CHECK(rc2 == 0, "Producer connesso al coordinatore riavviato sulla stessa porta");
    CHECK(file_contains("test2.log", "TEST_RIUSO"), "Dato scritto sul log dopo il riuso della porta");

    /* 3. TEST ROTAZIONE LOG SU SIGALRM */
    printf("\n--- 3. Test Rotazione Log su SIGALRM ---\n");
    fflush(stdout);
    pid_t p3 = avvia_coordinatore("8903", "test3.log", "100", "1");
    system("./bin/producer -p 8903 -i PROD_1 -d 123456789 -n 5 > /dev/null 2>&1");
    kill(p3, SIGALRM); /* Genera segnale SIGALRM: il file supera gia' 100 byte */
    usleep(300000);
    system("./bin/producer -p 8903 -i PROD_NUOVO -d 999 > /dev/null 2>&1");
    usleep(300000); /* Attendi che il coordinatore scriva sul nuovo log */
    kill(p3, SIGINT);
    waitpid(p3, NULL, 0);
    CHECK(count_glob("test3.log.*.bak") >= 1, "File di archivio (.bak) creato durante la rotazione");
    CHECK(any_glob_contains("test3.log.*.bak", "PROD_1"), "Vecchi dati archiviati nel file .bak");
    CHECK(file_contains("test3.log", "PROD_NUOVO"), "Nuovo log contiene i dati scritti dopo la rotazione");

    /* 4. TEST DISCONNESSIONE CLIENT (SIGPIPE) */
    printf("\n--- 4. Test Disconnessione Client (SIGPIPE) ---\n");
    fflush(stdout);
    pid_t p4 = avvia_coordinatore("8904", "test4.log", "10000", "10");
    system("./bin/producer -p 8904 -i PROD_ABRUPT -d 555 -c > /dev/null 2>&1");
    usleep(300000); /* Attendi che il coordinatore registri la disconnessione */
    int prima = count_lines_with("test4.log", "DISCONNECT");
    CHECK(prima >= 1, "Disconnessione produttore registrata nel log");

    /* Invia SIGPIPE direttamente al coordinatore: solo l'handler puo' scrivere
     * questa voce, quindi l'incremento prova che l'handler e' stato eseguito. */
    kill(p4, SIGPIPE);
    usleep(300000);
    int dopo = count_lines_with("test4.log", "DISCONNECT");
    CHECK(dopo == prima + 1, "Handler SIGPIPE ha scritto la voce DISCONNECT");

    kill(p4, SIGINT);
    waitpid(p4, NULL, 0);

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

    CHECK(count_glob("test5.log.*.bak") >= 1, "Almeno una rotazione eseguita durante lo stress test");
    CHECK(any_glob_contains("test5.log*", "PROD_STRESS_1"), "Dati di PROD_STRESS_1 presenti nel log");
    CHECK(any_glob_contains("test5.log*", "PROD_STRESS_4"), "Dati di PROD_STRESS_4 presenti nel log");

    /* Riepilogo finale */
    printf("\n===================================================\n");
    printf("   RISULTATO: %d PASS, %d FAIL\n", g_passed, g_failed);
    if (g_failed == 0) printf("   TUTTI I TEST SONO STATI VERIFICATI CON SUCCESSO!\n");
    else               printf("   CI SONO TEST FALLITI!\n");
    printf("===================================================\n");
    fflush(stdout);

    system("rm -f test*.log*");
    return (g_failed == 0) ? 0 : 1;
}
