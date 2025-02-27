# BitTorrent-Protocol
My BitTorrent protocol simulation implements decentralized file sharing using MPI and Pthreads. An MPI-based tracker (rank 0) coordinates file metadata and swarm updates, while clients concurrently download/upload segments using a round-robin approach with hash verification for data integrity.

The project is structured as follows: tema2.c contains the main code, 
while tema2.h includes structures, macros, and function prototypes. The tracker is implemented 
as an MPI task with rank 0, while the other tasks are clients. The tracker maintains information 
about available files and the clients sharing them.

At initialization, each client reads the input file, populating local structures with information 
about owned and requested files. The client sends a message to the tracker with the files it owns, 
and the tracker adds them to its internal list, marking the client as a seed for those files. 
Once all clients send their initialization messages, the tracker confirms with a 
message of type TAG_INIT_ACK that downloads can begin.

Clients download desired files by sending swarm requests to the tracker (TAG_SWARM_REQUEST). 
The tracker responds with a list of clients that own the file segments. 
The client downloads segments in a distributed manner, using a round-robin algorithm 
to select segment sources and avoid overloading a single peer. After every 10 segments downloaded, 
the client requests a swarm update for unfinished files from the tracker.

Upon completing a file download, the client notifies the tracker via a TAG_FILE_COMPLETE message. 
After downloading all desired files, the client sends a TAG_ALL_COMPLETE message to the tracker. 
The tracker monitors the status of all clients, and when all clients have completed their downloads, 
it sends TAG_STOP messages to signal the end of the process.

The algorithm is implemented using two threads for each client: one for downloading (download thread) 
and another for responding to requests from other clients (upload thread). The transfer of segments 
between clients is simulated through MPI messages that include details about the segment hashes.
