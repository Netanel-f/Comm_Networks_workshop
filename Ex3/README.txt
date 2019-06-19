README
------
The program is Server-Client application to test throughput of both protocols.
The code is build from handlers and setters for EAGER/RENDEZVOUS protocols.
in both cases a connection is setup using TCP/IP and then an IB connection is set between the two devices. Afterwards a throughput test is start running and taking times.

I encountered a critical error during RDMA operation so Part 2 results are missing.

Part 1 Results:
Value size: 1 Bytes, Throughput: 1.714 Mbps
Value size: 2 Bytes, Throughput: 24.421 Mbps
Value size: 4 Bytes, Throughput: 28.387 Mbps
Value size: 8 Bytes, Throughput: 34.894 Mbps
Value size: 16 Bytes, Throughput: 48.660 Mbps
Value size: 32 Bytes, Throughput: 64.141 Mbps
Value size: 64 Bytes, Throughput: 109.251 Mbps
Value size: 128 Bytes, Throughput: 16.450 Mbps
Value size: 256 Bytes, Throughput: 8.208 Mbps
Value size: 512 Bytes, Throughput: 3.497 Mbps
Value size: 1 KBytes, Throughput: 1.743 Mbps
Value size: 2 KBytes, Throughput: 4.698 Mbps

Part 2 Results:
