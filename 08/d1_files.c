#define _POSIX_C_SOURCE 200809L
#include <unistd.h>     /* execve, write, … */
#include <fcntl.h>      /* openat */
#include <sys/wait.h>   /* waitpid */
#include <stdbool.h>    /* bool */
#include <stdio.h>      /* snprintf */
#include <string.h>     /* strlen, strstr */
#include <stdlib.h>     /* malloc, free, abort */
#include <assert.h>
#include <err.h>

/* V této ukázce naprogramujeme trojici procedur, které nám umožní
 * pohodlně spouštět programy na datech, které máme nachystané
 * v paměti. Chceme-li programu předat víc než jeden souvislý blok
 * dat, nebude nám k tomu stačit standardní vstup.¹ Níže
 * implementované procedury tento problém řeší tak, že pro každý
 * vstup vytvoří dočasný soubor a tyto soubory předají programu jako
 * argumenty.
 *
 * Problém musíme rozdělit do několika podprogramů proto, že
 * sekvence spuštění sestává ze tří kroků:
 *
 *  1. vytvoření a naplnění dočasných souborů,
 *  2. spuštění programu,
 *  3. odstranění dočasných souborů,
 *
 * ale druhý krok nemůžeme provést v tom stejném procesu, jako ten
 * třetí, protože volání ‹exec› předá řízení spouštěnému programu a
 * tedy by již nebylo možné úklid provést.
 *
 * Zde navržené procedury tedy poslouží jako stavební kameny, které
 * lze kombinovat se systémovými voláními ‹fork› a ‹waitpid›
 * (kterými se budeme blíže zabývat příště) do kompletního řešení.
 * Každá procedura provede jeden z výše uvedených tří kroků. */

/* Data, která budeme programu předávat, uložíme do zřetězeného
 * seznamu ‹buffer_list›, který krom ukazatele na samotná data a
 * velikost těchto dat obsahuje vyhrazené místo pro název
 * příslušného dočasného souboru. Tento prostor bude sloužit pro
 * předání informace z procedury ‹prepare_files› do dalších dvou,
 * ‹execv_with_files› a ‹cleanup_files›. */

struct buffer_list
{
    char tmp_name[ 32 ];
    bool created;
    const char *data;
    int data_len;
    struct buffer_list *next;
};

/* První z trojice procedur, ‹prepare_files›, nachystá soubory pro
 * použití, resp. úklid, v dalších dvou. Krom toho, že soubory
 * vytvoří, také poznačí jejich jména do předaného seznamu. Protože
 * vytváření souborů může v libovolném bodě selhat, musíme být
 * připraveni již vytvořené soubory v takové situaci odstranit.
 *
 * S trochou plánování k tomu budeme moct využit proceduru
 * ‹cleanup_files›, čím si ušetříme práci (a duplikaci kódu). */

int cleanup_files( struct buffer_list *buffers );

int prepare_files( struct buffer_list *buffers )
{
    int fd = -1;

    for ( struct buffer_list *buf = buffers; buf; buf = buf->next )
    {
        int tmp_len = sizeof( buf->tmp_name ),
            buf_len = buf->data_len;

        /* Do pole ‹tmp_name› nachystáme šablonu pro knihovní
         * proceduru ‹mkstemp›, která tuto šablonu nahradí vhodným
         * názvem souboru. */

        snprintf( buf->tmp_name, tmp_len, ".input.XXXXXX" );

        fd = mkstemp( buf->tmp_name );

        if ( fd == -1 )
            goto error;

        /* V tuto chvíli víme, že soubor vznikl, a tedy jej musíme
         * v případě pozdější chyby odstranit. Proto si do položky
         * ‹created› tuto skutečnost poznačíme. Pozor, v případě, že
         * ‹mkstemp› skončí chybou, nesmíme soubor daného jména
         * odstraňovat, protože soubor tohoto jména může existovat
         * nezávisle na našem programu. */

        buf->created = true;

        /* Nezbývá, než do souboru zapsat data a popisovač zavřít.
         * Nepodaří-li se nám uložit celý řetězec, považujeme to za
         * fatální chybu a proceduru ukončíme. */

        if ( write( fd, buf->data, buf_len ) != buf_len )
            goto error;

        close( fd );
    }

    return 0;
error:
    if ( fd >= 0 )
        close( fd );

    cleanup_files( buffers );
    return -1;
}

/* Podprogram ‹execv_with_files› připraví argumenty pro externí
 * program a tento spustí (ve stávajícím procesu – očekává se, že
 * potřebný proces vytvoří v příhodné chvíli volající). Názvy
 * souborů procedura přidá za parametry, které obdrží v poli
 * ‹argv_prefix›, které má stejnou strukturu jako parametr volání
 * ‹execv›. */

int execv_with_files( const char *cmd,
                      char * const argv_prefix[],
                      struct buffer_list *buffers )
{
    /* Abychom mohli předat parametry systémovému volání ‹execv›,
     * musíme je nachystat do pole ukazatelů. Velikost tohoto pole
     * závisí na počtu argumentů v ‹argv› a také na počtu souborů
     * v seznamu ‹buffers›. Nesmíme zapomenout na nulový ukazatel,
     * kterým pole zakončíme. */

    int argv_size = 1, i = 0;

    for ( char * const *arg = argv_prefix; *arg; ++arg )
        ++ argv_size;

    for ( struct buffer_list *buf = buffers; buf; buf = buf->next )
        ++ argv_size;

    /* Nyní známe potřebnou velikost. Stojíme nyní před rozhodnutím,
     * jak získat paměť pro výsledné pole argumentů. Jsme-li
     * v situaci, kdy můžeme omezení velikosti nastavit jako vstupní
     * podmínku procedury ‹execv_with_files›, bylo by lze využít
     * automatické pole proměnné velikosti (VLA). V obecném případě
     * to ale možné není a paměť musíme alokovat dynamicky. */

    char **argv = malloc( sizeof( char * ) * argv_size );

    if ( !argv )
        return -1;

    for ( char * const *arg = argv_prefix; *arg; ++arg )
        argv[ i++ ] = *arg;

    for ( struct buffer_list *buf = buffers; buf; buf = buf->next )
        argv[ i++ ] = buf->tmp_name;

    argv[ i ] = NULL;

    /* Nyní je již vše připraveno pro volání ‹execv›. Protože
     * výsledkem ‹execv› může být pouze chyba, nemusíme jeho
     * návratovou hodnotu kontrolovat – v případě úspěchu se
     * řízení do procedury ‹execv_with_files› již nevrátí. */

    execv( cmd, argv );

    /* Dostaneme-li se do tohoto místa, musela nastat chyba při
     * spuštění. Uvolníme alokovanou dynamickou paměť a vrátíme
     * chybový kód. */

    free( argv );
    return -1;
}

/* Konečně procedura ‹cleanup_files› provede úklid dočasných
 * souborů. Nesmíme zapomenout, že jsme ‹cleanup_files› použili
 * v chybové cestě procedury ‹prepare_files›, musíme tedy
 * kontrolovat, zda byl soubor skutečně vytvořen (resp. zda
 * ‹tmp_name› obsahuje platný název dočasného souboru).*/

int cleanup_files( struct buffer_list *buffers )
{
    int rv = 0;

    /* Nepodaří-li se některý dočasný soubor odstranit, úklid
     * nepřerušíme a pokusíme se odstranit alespoň soubory
     * zbývající. Chybu si ale poznačíme, abychom mohli volajícímu
     * oznámit, že úklid se nepodařilo provést kompletně. */

    for ( struct buffer_list *buf = buffers; buf; buf = buf->next )
        if ( buf->created && unlink( buf->tmp_name ) == -1 )
            rv = -1;

    return rv;
}

/* ¹ Přesměrováni standardního vstupu je běžný způsob, jak programu
 *   předat jeden blok, resp. proud dat. To nám ale nenabízí možnost
 *   rozdělit data do více logických celků, a tedy nebude stačit
 *   například v situaci, kdybychom chtěli na dvou řetězcích spustit
 *   standardní program ‹diff›, který nalezne řádky, ve kterých se
 *   tyto dva řetězce liší. */

/* Nyní již nezbývá, než procedury otestovat. Níže definujeme
 * testovací proceduru ‹fork_with_files›, která pouze provede
 * ‹execv_with_files› v novém procesu a vyčká na jeho ukončení.
 * Standardní výstup nového procesu uloží do pole ‹output›. */

static int fork_with_files( const char *cmd, char * const argv[],
                            struct buffer_list *buffers,
                            char *output, int out_len );

int main() /* demo */
{
    char buffer[ 512 ] = { 0 };

    struct buffer_list
        bye   = { .data = "bye\nworld\n",   .next = NULL },
        hello = { .data = "hello\nworld\n", .next = &bye };

    for ( struct buffer_list *buf = &hello; buf; buf = buf->next )
        buf->data_len = strlen( buf->data );

    char *argv[] = { "diff", "-u", NULL };

    assert( prepare_files( &hello ) == 0 );
    assert( fork_with_files( "/usr/bin/diff", argv, &hello,
                             buffer, sizeof buffer ) == 1 );
    assert( cleanup_files( &hello ) == 0 );

    assert( strstr( buffer, "+bye\n" ) );
    assert( strstr( buffer, "-hello\n" ) );
    assert( strstr( buffer, " world\n" ) );

    return 0;
}

/* ┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄┄┄┄ následují testy ┄┄┄┄┄┄┄┄┄┄ %< ┄┄┄┄┄┄┄ */

static int fork_with_files( const char *cmd, char * const argv[],
                            struct buffer_list *buffers,
                            char *output, int out_len )
{
    int fds[ 2 ];

    if ( pipe( fds ) == -1 )
        err( 1, "pipe" );

    pid_t pid = fork();

    if ( pid == -1 )
        err( 2, "fork" );

    if ( pid == 0 )
    {
        dup2( fds[ 1 ], 1 );
        close( fds[ 0 ] );
        close( fds[ 1 ] );
        execv_with_files( cmd, argv, buffers );
        exit( 100 ); /* 1 is success from diff */
    }

    if ( pid > 0 )
    {
        close( fds[ 1 ] );
        int status, bytes = 0, total = 0;

        while ( ( bytes = read( fds[ 0 ], output + total,
                                          out_len - total ) ) > 0 )
            total += bytes;

        close( fds[ 0 ] );

        if ( bytes == -1 )
            err( 2, "reading from pipe" );

        if ( waitpid( pid, &status, 0 ) == -1 )
            err( 2, "wait" );

        if ( WIFEXITED( status ) )
            return WEXITSTATUS( status );
        else
            return -1;
    }

    abort();
}
