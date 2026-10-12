# SoftFloat F/D arithmetic subset

Copied byte-for-byte from the fixed Spike archive listed in manifest.json.
C arithmetic only; RVISS instruction encoding, permissions, register state,
boxing, CSR, and memory behavior are implemented in src/target/riscv64.
config.h supplies host endianness for upstream platform.h. LICENSE and all
original copyright headers are retained. The independent Spike process uses
its separately built upstream library; arithmetic common-mode risk is covered
by normative constant tests as well as instruction-level differential tests.
