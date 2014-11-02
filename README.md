Finite State Coder

This project is an experimental implementation of Jarek Duda's assymetric
numeral systems (ANS), as described in the following paper:

http://arxiv.org/abs/1311.2540

To understand ANS, i extensively used the FSE project:

https://github.com/Cyan4973/FiniteStateEntropy

from Yann Collet, which is referenced by Jarek's paper.
See the blog entry:
  http://fastcompression.blogspot.fr/2013/12/finite-state-entropy-new-breed-of.html

Fabian Giesen also has interesting implementations. See his blog for pointers:

http://fgiesen.wordpress.com/

Code is located here: https://github.com/rygorous/ryg_rans

-------------------

This implementation tries two approaches: bit-by-bit encoding/decoding,
or byte-by-byte, where bits are groupes in packets of 8bits.
Note that the coder still emits bits one by one though (b=2, in the
ANS terminology). Experimenting with b=256 or more is coming next...

There are several 'Spread Function' available to try different
symbol <-> slots assignments (see BuildSpreadTableXXX() functions).
You can switch from one to another in the command line most of the time.

Known limitations:
  - alphabet size should be <= 256
  - max table size is 2 ^ 14

-------------------

Command line help:

```
./fsc -h
usage: ./fsc [options] < in_file > out_file
options:
-c           : compression mode (default)
-d           : decompression mode
-s           : don't emit output, just print stats
-l           : change log-table-size (in [2..14], default 12)
-mod         : use modulo spread function
-rev         : use reverse spread function
-exp         : use experimental spread function
-bucket      : use bucket-sort spread function (default)
-h           : this help


./test -h
usage: ./test [options] [size]
options:
-t <int>           : distribution type (in [0..5])
-p <int>           : distribution param (>=0)
-s <int>           : number of symbols (in [2..256]))
-l <int>           : max table size bits (<= LOG_TAB_SIZE)
-save <string>     : save input message to file
-d                 : print distribution
-f <string>        : message file name
-mod               : use modulo spread function
-rev               : use reverse spread function
-h                 : this help


./bit_test -h
usage: ./bit_test [options] [size]
-l <int>           : max table size bits for bit-by-bit
-l8 <int>          : max table size bits for byte-by-byte
-p <int>           : try only one proba value
-mod               : use modulo spread function
-rev               : use reverse spread function
-fsc               : skip FSC
-fsc8              : skip FSC8
-h                 : this help
```
