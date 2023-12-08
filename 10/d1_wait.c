#define _POSIX_C_SOURCE 200809L

/* In this program, we will learn how to fork a child process and
 * check its result after it finishes executing. */

#include <unistd.h>   /* fork */
#include <sys/wait.h> /* waitpid */
#include <stdlib.h>   /* exit */
#include <assert.h>
#include <err.h>

int main()
{
    /* ‹fork› takes no arguments and returns a ‹pid_t›, which is an
     * integer with platform-defined size (big enough to hold any
     * valid process id) */

    pid_t pid = fork();

    if ( pid < 0 )

        /* Things went south; you can look at ‹man fork› to check
         * what could cause ‹fork› to fail (specifically section
         * ERRORS near the end of the manpage).  */

        err( 1, "fork" );

    /* Fork seemingly returns twice: once in the parent process and
     * once in the child process. In the parent process, ‹fork()›
     * returns the pid of the newly created child. In the child
     * process, it returns 0.  If an error happens, it returns -1
     * and no process is created: hence, in this case, fork() only
     * returns once. */

    if ( pid == 0 )
        exit( 13 ); /* terminate with exit code 13 */

    if ( pid > 0 )
    {
        /* The child process is running, but we don't know when it
         * finishes or how. To find out, we can use the ‹waitpid›
         * system call. It is only allowed to call ‹waitpid› on
         * processes that are direct descendants (via ‹fork›) of the
         * current one. As usual, see ‹man waitpid›. Besides waiting
         * for the child process to finish, we can learn the exit
         * status, or whether the process exited normally or was
         * killed. */

        int status;

        if ( waitpid( pid, &status, 0 ) == -1 )
            err( 1, "waitpid" );

        /* The integer ‹status› passed to waitpid is filled in with
         * information about how the process terminated.
         * Unfortunately, it is a single integer, not a structure
         * like ‹struct stat›, which makes things a little odd and
         * complicated. We have to use macros called ‹WIFEXITED› and
         * ‹WEXITSTATUS› to learn whether the program terminated
         * normally and what was the exit code. The remaining macros
         * (described in ‹man waitpid›) deal with signals -- you can
         * study those on your own, or perhaps in a more advanced
         * course on POSIX programming. */

        assert( WIFEXITED( status ) );
        assert( WEXITSTATUS( status ) == 13 );
    }

    return 0; /* terminate with exit code 0 */
}
