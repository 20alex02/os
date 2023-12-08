wrap_line()
{
    indent=$(printf "$1" | wc -c);
    width=$((100 - $indent - 1)); # FIXME widen $COLUMNS in frag-eval
    c1="$1"; shift
    c2="$@"
    nl=
    while test -n "$c2"; do
        printf "$nl%-${indent}s%.${width}s" "$c1" "$c2"
        c2="$(echo "$c2" | cut -c$(($width + 1))-)";
        c1=
        nl="\\\\""\n"; # "
    done
    echo
}

wrap()
{
    while IFS= read -r line; do wrap_line "" "$line"; done
}

log_format()
{
    count=$(wc -l < $1)
    limit=${max_lines:-120}

    if test $count -gt $limit; then
        head -n 15 < $1 | wrap
        echo "[$(( $count - $limit )) lines of output suppressed]"
        tail -n $(( $limit - 15 )) < $1 | wrap
    else
        wrap < $1
    fi > $1.tmp

    mv $1.tmp $1
}

tidy()
{
    src=$1
    tidy_base=$(echo $1 | sed -re 's,(student/|)(.*)([.]s|)[.]cpp,\2.p.cpp,')

    tidy_extra=
    for tidy_f in $(ls $tidy_base eval/$tidy_base 2> /dev/null); do
        tidy_x=$(perl -ne 'if ( m,// clang-tidy: (.*), ) { print "-checks=$1" }' < $tidy_f)
        tidy_extra="$tidy_extra $tidy_x"
    done

    if ! $TIDY --warnings-as-errors='*' $tidy_extra --quiet $src -- $TDYFLAGS; then
        echo 'clang-tidy failed'; return 1
    else
        return 0
    fi
}

announce()
{
    printf "%-16s " $(echo "$1" | sed -e 's,^sol\.,,;s,.c$,,')
}

xsrc()
{
    hdr=$(echo "$@" | xargs -n1 echo | grep -v ^- | grep \.c$ | sed -e s,\.c,.h,)
    for f in $hdr; do if test -f $f; then cat $f; fi; done | \
        perl -ne 'if ( m,// extra files: (.*), ) { print $1 }' | xargs -n1 echo | grep c$
}

get_xflags()
{
    XFLAGS="$(perl -ne 'if ( m,// cflags: (.*), ) { print $1 }' < $1)"
    if test $(uname) = Linux; then XFLAGS="$XFLAGS -ldl"; fi
}

cxx()
{
    get_xflags "$1"
    if ! log $CXX $CXXFLAGS "$@" $EXTRA_SRC $(xsrc "$@") $XFLAGS; then
        echo 'build failed'; return 1
    else
        return 0
    fi
}

cc()
{
    get_xflags "$1"
    if ! log $CC $CFLAGS "$@" $EXTRA_SRC $(xsrc "$@") $XFLAGS; then
        echo 'build failed'; return 1
    else
        return 0
    fi
}

build()
{
    if test -x $1; then
        ln -f $1 a.out
    else
        if ! cc  "$@"; then return 1; fi
        if ! tidy $1; then return 1; fi
    fi

    return 0
}

grind()
{
    vglog=.valgrind.$base.out

    rm -f $vglog.*
    log $VALGRIND --error-exitcode=1 --log-file=$vglog.%p --leak-check=full "$@"
    err=$?

    for log in $vglog.*; do
        if grep -q "ERROR SUMMARY: [1-9]" $log; then
            cat $log 1>&2
        fi
        if printf "%s " "$@" | grep -q -- --quiet && test -s $log; then
            cat $log 1>&2
        fi
    done

    return $err
}

check()
{
    if ! build "$@"; then return 1; fi
    if ! ./a.out; then return 1; fi
    if ! grind ./a.out; then return 1; fi

    echo 'ok'; return 0
}
action=$1 ; shift
source=$1 ; shift
base=$(basename $source .$suffix)
CXXFLAGS="$CXXFLAGS -MD -MP -MF .$(basename $source .cpp).d"

log() { echo "$@" ; "$@" ; }

$action $source "$@"
