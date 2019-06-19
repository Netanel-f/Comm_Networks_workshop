README
------
The program is Server-Client application to test throughput of both protocols.
The code is build from handlers and setters for EAGER/RENDEZVOUS protocols.
in both cases a connection is setup using TCP/IP and then an IB connection is set between the two devices.
Afterwards a throughput test is start running and taking times.
I have set a global boolean variable called 'use_rndv_protocol' which will enforce the use
of EAGER / Rendezvous accordingly.


Part 1 Results - Eager protocol:
Value size: 1	Bytes,	Throughput: 51.604	Mbps
Value size: 2	Bytes,	Throughput: 10.774	Mbps
Value size: 4	Bytes,	Throughput: 10.938	Mbps
Value size: 8	Bytes,	Throughput: 59.833	Mbps
Value size: 16	Bytes,	Throughput: 803.765	Kbps
Value size: 32	Bytes,	Throughput: 266.269	Kbps
Value size: 64	Bytes,	Throughput: 206.581	Kbps
Value size: 128	Bytes,	Throughput: 397.661	Kbps
Value size: 256	Bytes,	Throughput: 865.081	Kbps
Value size: 512	Bytes,	Throughput: 1.180	Mbps
Value size: 1	KBytes,	Throughput: 2.585	Mbps
Value size: 2	KBytes,	Throughput: 4.798	Mbps


Part 2 Results - Rendezvous protocol:

Value size: 1	Bytes,	Throughput: 18.598	Mbps
Value size: 2	Bytes,	Throughput: 19.910	Mbps
Value size: 4	Bytes,	Throughput: 20.724	Mbps
Value size: 8	Bytes,	Throughput: 22.096	Mbps
Value size: 16	Bytes,	Throughput: 27.001	Mbps
Value size: 32	Bytes,	Throughput: 32.390	Mbps
Value size: 64	Bytes,	Throughput: 46.670	Mbps
Value size: 128	Bytes,	Throughput: 71.665	Mbps
Value size: 256	Bytes,	Throughput: 127.275	Mbps
Value size: 512	Bytes,	Throughput: 236.903	Mbps
Value size: 1	KBytes,	Throughput: 518.295	Mbps
Value size: 2	KBytes,	Throughput: 976.178	Mbps
Value size: 4	KBytes,	Throughput: 2.050	Gbps
Value size: 8	KBytes,	Throughput: 4.251	Gbps
Value size: 16	KBytes,	Throughput: 7.457	Gbps
Value size: 32	KBytes,	Throughput: 10.301	Gbps
