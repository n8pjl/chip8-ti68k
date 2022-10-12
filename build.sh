#!/bin/sh
rm -rf output
mkdir output/
tigcc -std=gnu99 -Os startup.c opcodes.c sprite.c -o output/ch8ti -Wall -Wextra -DUSE_TI89 --native
tigcc -std=gnu99 -Os startup.c opcodes.c sprite.c -o output/ch8ti -Wall -Wextra -DUSE_TI92P --native
tigcc -std=gnu99 -Os startup.c opcodes.c sprite.c -o output/ch8ti -Wall -Wextra -DUSE_V200 --native
cd preprocessor
cargo build --release
cargo build --target=x86_64-pc-windows-gnu --release
cp target/release/ch8ti-prep ../output/.
cp target/x86_64-pc-windows-gnu/release/ch8ti-prep.exe ../output/.
cd ..

echo "Done"
