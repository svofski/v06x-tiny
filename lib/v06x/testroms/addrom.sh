#!/bin/bash
bname=`basename $1 .rom`
cname=$bname.c
romvar=$bname"_rom[]"
romlen=$bname"_rom_len"

echo $1 '->' $cname $romvar $romlen
echo const `xxd -i $1` > $cname
cat << BOB | ex -n ../src/testroms.h
/}
i
    extern unsigned char $romvar;
    extern unsigned int $romlen;
.
w
q
BOB 
