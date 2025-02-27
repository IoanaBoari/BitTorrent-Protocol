# BitTorrent-Protocol
My BitTorrent protocol simulation implements decentralized file sharing using MPI and Pthreads. An MPI-based tracker (rank 0) coordinates file metadata and swarm updates, while clients concurrently download/upload segments using a round-robin approach with hash verification for data integrity.
