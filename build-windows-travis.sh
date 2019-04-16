export PATH==/cygdrive/c/tools/cygwin/bin:$PATH

echo "Make deps"
make deps || true

echo "Make dist"
make dist
