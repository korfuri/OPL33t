export PATH=/cygdrive/c/tools/cygwin/bin:$PATH; echo "Path inside build-windows-travis.sh: $PATH"; echo "Make deps"; make deps LIB_EXTENSION=lib; echo "Make dist"; make dist LIB_EXTENSION=lib;
