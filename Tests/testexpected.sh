#!/bin/sh

LIBSUFFIX=so
if [ "$(uname)" == "Darwin" ]; then
	LIBSUFFIX=dylib
fi

echo $1/igk --plugin "$1/libigk-clang.$LIBSUFFIX" --lua-directory "$2/lua/" --pass $4 --file "$3.in" --pass TeXOutputPass \| diff -u "$3.out" -
$1/igk --plugin "$1/libigk-clang.$LIBSUFFIX" --lua-directory "$2/lua/" --pass $4 --file "$3.in" --pass TeXOutputPass | diff -u "$3.out" -
