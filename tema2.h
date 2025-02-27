#ifndef TEMA2_H
#define TEMA2_H

#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TRACKER_RANK     0
#define MAX_FILES        10
#define MAX_FILENAME     15
#define HASH_SIZE        32
#define MAX_CHUNKS       100

#define MAX_CLIENTS 10

#define TAG_INIT         1       // Tag for client-to-tracker initialization message
#define TAG_INIT_ACK     2       // Tag for tracker's acknowledgment to client initialization
#define TAG_SWARM_REQUEST 3      // Tag for client requesting swarm information
#define TAG_SWARM_RESPONSE 4     // Tag for tracker sending swarm response
#define TAG_REQUEST_SEGMENT 5    // Tag for client-to-client segment request
#define TAG_SEND_SEGMENT 6       // Tag for sending a segment from one client to another
#define TAG_FILE_COMPLETE 7      // Tag for client notifying tracker of file completion
#define TAG_ALL_COMPLETE 8       // Tag for client notifying tracker of all files completed
#define TAG_STOP         9       // Tag for tracker signaling clients to stop

/* Structure to represent a file owned by a client */
typedef struct {
    char fileName[MAX_FILENAME];
    int  nr_segments;
    char hashes[MAX_CHUNKS][HASH_SIZE+1];
    int  segmentExists[MAX_CHUNKS];
} OwnedFile;

/* Structure to represent a file a client wants to download */
typedef struct {
    char fileName[MAX_FILENAME];
    int  nr_segmentsNeeded;
    char downloadedHashes[MAX_CHUNKS][HASH_SIZE+1];
    int  downloadedSegmentExists[MAX_CHUNKS];
    int current_peer_index;
} WantedFile;

/* Main structure representing a client's data */
typedef struct {
    int rank;
    int numOwned;
    OwnedFile  ownedFiles[MAX_FILES];
    int numWanted;
    WantedFile wantedFiles[MAX_FILES];
    int allComplete;                 // Flag indicating if the client has completed all downloads
} ClientData;

extern ClientData clients[MAX_CLIENTS]; // Global array storing data for all clients

/* Structure for initializing file details in client-to-tracker communication */
typedef struct {
    char fileName[MAX_FILENAME];
    int  nr_segments;
} InitFileEntry;

/* Structure for a client-to-tracker initialization message */
typedef struct {
    int numFiles;
    InitFileEntry files[MAX_FILES];
} InitMessage;

/* Enum representing the status of a client in the swarm */
typedef enum { 
    SEED,  // Client acts as a seed, sharing all segments of a file
    PEER   // Client acts as a peer, sharing and downloading segments of a file
} SwarmStatus;

/* Structure representing a client in a file's swarm */
typedef struct {
    int rank;
    SwarmStatus status;
    int loadCount;      // Number of active requests being handled by this client
} SwarmClient;

/* Structure representing a file being tracked by the tracker */
typedef struct {
    char fileName[MAX_FILENAME];
    int  nr_segments;
    SwarmClient swarm[MAX_CLIENTS];
    int swarmSize;
} TrackerFile;

/* Structure managing the tracker's global state */
typedef struct {
    TrackerFile files[2*MAX_FILES];
    int numFiles;
    int finishedAll[MAX_CLIENTS];   // Flags indicating which clients have completed all downloads
    int totalClients;
} TrackerData;

/* Structure representing the response to a swarm request from a client */
typedef struct {
    int swarmSize;
    int totalSegments;
    SwarmClient info[MAX_CLIENTS];
} SwarmResponse;

/* Structure representing a request for a specific segment */
typedef struct {
    char fileName[MAX_FILENAME];
    int  segmentIndex;
} SegmentRequest;

// Function prototypes
void parseInputFile(ClientData *cd);
void sendInitMessage(ClientData *cd);
void trackerAddFile(TrackerData *td, const char* fname, int numSeg, int cRank);
void handleInit(TrackerData *td, int source);
int addPeerToSwarm(TrackerData *td, const char* fname, int cRank);
void handleSwarmRequest(TrackerData *td, int source);
void handleFileComplete(TrackerData *td, int source);
void handleAllComplete(TrackerData *td, int source, int numtasks);
int allFinished(TrackerData *td, int numtasks);
void *upload_thread_func(void *arg);
void *download_thread_func(void *arg);
void tracker(int numtasks, int rank);
void peer(int numtasks, int rank);

#endif // TEMA2_H