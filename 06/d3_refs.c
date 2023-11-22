#define _POSIX_C_SOURCE 200809L

#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <errno.h>
#include <err.h>
#include <assert.h>

int count_refs_rec( int root_fd, dev_t dev, ino_t ino, int *count )
{
    if ( root_fd == -1 )
        return -1;

    int rv = -1;
    DIR *dir = fdopendir( root_fd );

    if ( !dir )
        goto out;

    rewinddir( dir );

    struct dirent *ptr;
    struct stat st;

    while ( ( ptr = readdir( dir ) ) )
    {
        if ( strcmp( ptr->d_name, "." ) == 0 ||
             strcmp( ptr->d_name, ".." ) == 0 )
            continue;

        if ( fstatat( root_fd, ptr->d_name, &st,
                      AT_SYMLINK_NOFOLLOW ) == -1 )
            goto out;

        if ( st.st_dev == dev && st.st_ino == ino )
        {
            dprintf( 2, "found copy at %s\n", ptr->d_name );
            ++ *count;
        }

        if ( S_ISDIR( st.st_mode ) )
        {
            int sub_fd = openat( root_fd, ptr->d_name,
                                 O_DIRECTORY | O_RDONLY );

            if ( count_refs_rec( sub_fd, dev, ino, count ) == -1 )
                goto out;
        }
    }

    rv = 0;
out:
    if ( dir )
        closedir( dir );

    return rv;
}

int count_refs( int root_fd, int file_fd )
{
    struct stat st;
    int count = 0;

    if ( fstat( file_fd, &st ) == -1 )
        return -1;

    if ( count_refs_rec( dup( root_fd ), st.st_dev,
                         st.st_ino, &count ) == -1 )
        return -1;

    return count;
}

static int mkdir_or_die( int dir_fd, const char *name )
{
    int fd;

    if ( mkdirat( dir_fd, name, 0777 ) == -1 && errno != EEXIST )
        err( 1, "creating directory %s", name );
    if ( ( fd = openat( dir_fd, name, O_DIRECTORY ) ) == -1 )
        err( 1, "opening newly created directory %s", name );

    return fd;
}

static int create_file( int dir_fd, const char *name )
{
    int fd = openat( dir_fd, name, O_CREAT | O_WRONLY, 0666 );

    if ( fd == -1 )
        err( 1, "creating file %s", name );

    return fd;
}

static void link_or_die( int fd_1, const char *name_1,
                         int fd_2, const char *name_2 )
{
    if ( unlinkat( fd_2, name_2, 0 ) == -1 && errno != ENOENT )
        err( 1, "unlinking %s", name_2 );
    if ( linkat( fd_1, name_1, fd_2, name_2, 0 ) == -1 )
        err( 1, "linking %s to %s", name_1, name_2 );
}

int main( void ) /* demo */
{
    int fds[ 5 ];

    fds[ 0 ] = mkdir_or_die( AT_FDCWD, "zt.d3_root" );
    fds[ 1 ] = mkdir_or_die( fds[ 0 ], "a" );
    fds[ 2 ] = mkdir_or_die( fds[ 0 ], "b" );
    fds[ 3 ] = mkdir_or_die( fds[ 2 ], "c" );
    fds[ 4 ] = mkdir_or_die( fds[ 3 ], "d" );

    int fd_1 = create_file( fds[ 0 ], "foo_1" );
    int fd_2 = create_file( fds[ 1 ], "bar_1" );

    link_or_die( fds[ 0 ], "foo_1", fds[ 0 ], "foo_2" );
    link_or_die( fds[ 0 ], "foo_1", fds[ 1 ], "foo_3" );
    link_or_die( fds[ 1 ], "bar_1", fds[ 2 ], "bar_2" );

    assert( count_refs( fds[ 0 ], fd_1 ) == 3 );
    assert( count_refs( fds[ 1 ], fd_1 ) == 1 );
    assert( count_refs( fds[ 1 ], fd_2 ) == 1 );
    assert( count_refs( fds[ 2 ], fd_2 ) == 1 );
    assert( count_refs( fds[ 2 ], fd_1 ) == 0 );

    for ( int i = 0; i < 5; ++i )
        close( fds[ i ] );

    close( fd_1 );
    close( fd_2 );

    return 0;
}
