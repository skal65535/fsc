Finite State Coder

This project is an experimental implementation of Jarek Duda's assymetric
numeral systems (ANS), as described in the following paper:

http://arxiv.org/abs/1311.2540

Namely, it implemented the _tabulated_ version of ANS, or tANS for
short.

Fabien Giesen also has interesting implementations. See his
blog for pointers:

http://fgiesen.wordpress.com/

Code is located here: https://github.com/rygorous/ryg_rans

-------------------

This implementation tries two approaches: bit-by-bit encoding/decoding,
or byte-by-byte, where bits are groupes in packets of 8bits.

Known limitations:
  - alphabet size should be <= 256
  - max table size is 2 ^ 14


