#include "tema2.h"

ClientData clients[MAX_CLIENTS];

void parseInputFile(ClientData *cd)
{
    char fname[64];
    sprintf(fname, "in%d.txt", cd->rank);

    FILE *fin = fopen(fname, "r");
    if (!fin) {
        // Abort if the input file cannot be opened
        fprintf(stderr, "Error while opening the file. %s\n", fname);
        MPI_Abort(MPI_COMM_WORLD, 1);
    }

    // Read the number of owned files and initialize their details
    int nr_owned;
    fscanf(fin, "%d", &nr_owned);
    cd->numOwned = 0;

    for (int i = 0; i < nr_owned; i++) {
        char fName[MAX_FILENAME];
        int numSeg;
        fscanf(fin, "%s %d", fName, &numSeg); // Read file name and number of segments

        OwnedFile *of = &cd->ownedFiles[cd->numOwned];
        memset(of, 0, sizeof(*of));

        strcpy(of->fileName, fName); // Set the file name
        of->nr_segments = numSeg;   // Set the number of segments

        // Read segment hashes
        for (int s = 0; s < numSeg; s++) {
            fscanf(fin, "%s", of->hashes[s]);
            of->segmentExists[s] = 1; // Mark segment as owned
        }
        cd->numOwned++;
    }

    // Read the number of wanted files and initialize their details
    fscanf(fin, "%d", &cd->numWanted);
    for (int i = 0; i < cd->numWanted; i++) {
        WantedFile *wf = &cd->wantedFiles[i];
        memset(wf, 0, sizeof(*wf));

        fscanf(fin, "%s", wf->fileName);
        wf->nr_segmentsNeeded = 0;      // Initialize needed segments to 0 (set later)
    }

    fclose(fin);
}

void sendInitMessage(ClientData *cd)
{
    InitMessage msg;
    memset(&msg, 0, sizeof(msg));
    msg.numFiles = cd->numOwned;

    // Populate file details in the message
    for (int i = 0; i < cd->numOwned; i++) {
        strcpy(msg.files[i].fileName, cd->ownedFiles[i].fileName); // Set file name
        msg.files[i].nr_segments = cd->ownedFiles[i].nr_segments; // Set number of segments
    }

    // Send the initialization message to the tracker
    MPI_Send(&msg, sizeof(msg), MPI_BYTE, TRACKER_RANK, TAG_INIT, MPI_COMM_WORLD);
}

void trackerAddFile(TrackerData *td, const char* fname, int numSeg, int cRank)
{
    int idx = -1;

    // Search for the file in the tracker's database
    for (int i = 0; i < td->numFiles; i++) {
        if (strcmp(td->files[i].fileName, fname) == 0) {
            idx = i; // File found
            break;
        }
    }

    // If the file is not found, add it to the tracker's database
    if (idx < 0) {
        idx = td->numFiles++;
        strcpy(td->files[idx].fileName, fname);
        td->files[idx].nr_segments = numSeg;
        td->files[idx].swarmSize = 0;
    }

    TrackerFile *tf = &td->files[idx];

    // Check if the client is already in the swarm
    for (int i = 0; i < tf->swarmSize; i++) {
        if (tf->swarm[i].rank == cRank) {
            tf->swarm[i].status = SEED; // Mark the client as a SEED
            return;
        }
    }

    // Add the client to the swarm as a SEED
    int sidx = tf->swarmSize++;
    tf->swarm[sidx].rank = cRank;
    tf->swarm[sidx].status = SEED;
    tf->swarm[sidx].loadCount = 0; // Initialize load count
}

void handleInit(TrackerData *td, int source)
{
    InitMessage msg;

    // Receive the initialization message from the client
    MPI_Recv(&msg, sizeof(msg), MPI_BYTE, source, TAG_INIT, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Add each file to the tracker's database
    for (int i = 0; i < msg.numFiles; i++) {
        trackerAddFile(td, msg.files[i].fileName, msg.files[i].nr_segments, source);
    }
}

int addPeerToSwarm(TrackerData *td, const char* fname, int cRank)
{
    int idx = -1;

    // Search for the file in the tracker's database
    for (int i = 0; i < td->numFiles; i++) {
        if (strcmp(td->files[i].fileName, fname) == 0) {
            idx = i; // File found
            break;
        }
    }

    // If the file is not found, add it to the tracker's database
    if (idx < 0) {
        idx = td->numFiles++;
        strcpy(td->files[idx].fileName, fname);
        td->files[idx].nr_segments = 999; // Placeholder for total segments
        td->files[idx].swarmSize = 0;
    }

    TrackerFile *tf = &td->files[idx];

    // Check if the client is already in the swarm
    for (int i = 0; i < tf->swarmSize; i++) {
        if (tf->swarm[i].rank == cRank) {
            tf->swarm[i].status = PEER; // Mark the client as a PEER
            return idx;
        }
    }

    // Add the client to the swarm as a PEER
    int sidx = tf->swarmSize++;
    tf->swarm[sidx].rank = cRank;
    tf->swarm[sidx].status = PEER;
    tf->swarm[sidx].loadCount = 0; // Initialize load count

    return idx;
}

void handleSwarmRequest(TrackerData *td, int source)
{
    char fileName[MAX_FILENAME];
    MPI_Recv(fileName, MAX_FILENAME, MPI_CHAR, source, TAG_SWARM_REQUEST, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Add the client to the swarm for the requested file
    int idx = addPeerToSwarm(td, fileName, source);
    TrackerFile *tf = &td->files[idx];

    // Prepare the swarm response
    SwarmResponse resp;
    memset(&resp, 0, sizeof(resp));

    resp.swarmSize = tf->swarmSize;
    resp.totalSegments = tf->nr_segments;

    for (int i = 0; i < tf->swarmSize; i++) {
        resp.info[i].rank = tf->swarm[i].rank;
        resp.info[i].status = (tf->swarm[i].status == SEED) ? 0 : 1;
        resp.info[i].loadCount = tf->swarm[i].loadCount;
    }

    // Send the swarm data back to the client
    MPI_Send(&resp, sizeof(resp), MPI_BYTE, source, TAG_SWARM_RESPONSE, MPI_COMM_WORLD);
}

void handleFileComplete(TrackerData *td, int source)
{
    char fileName[MAX_FILENAME];
    MPI_Recv(fileName, MAX_FILENAME, MPI_CHAR, source, TAG_FILE_COMPLETE, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    // Find the file in the tracker's database
    int idx = -1;
    for (int i = 0; i < td->numFiles; i++) {
        if (strcmp(td->files[i].fileName, fileName) == 0) {
            idx = i;
            break;
        }
    }

    // If the file is not found, return
    if (idx < 0) return;

    TrackerFile *tf = &td->files[idx];
    
    // Mark the client as a SEED for this file
    for (int i = 0; i < tf->swarmSize; i++) {
        if (tf->swarm[i].rank == source) {
            tf->swarm[i].status = SEED;
            break;
        }
    }
}

int allFinished(TrackerData *td, int numtasks)
{
    for (int r = 1; r < numtasks; r++) {
        if (!td->finishedAll[r]) return 0; // If any client is not finished, return 0
    }
    return 1; // All clients are finished
}

void handleAllComplete(TrackerData *td, int source, int numtasks)
{
    MPI_Status st;
    MPI_Recv(NULL, 0, MPI_CHAR, source, TAG_ALL_COMPLETE, MPI_COMM_WORLD, &st);

    // Mark this client as finished
    td->finishedAll[source] = 1;

    // Check if all clients are finished using the allFinished function
    if (allFinished(td, numtasks)) {
        // If all clients are finished, send stop messages
        for (int client = 1; client < numtasks; client++) {
            MPI_Send(NULL, 0, MPI_CHAR, client, TAG_STOP, MPI_COMM_WORLD);
        }
    }
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;
    ClientData *cd = &clients[rank];

    int stop = 0; // Flag to control when to stop the thread
    while (!stop) {
        MPI_Status st;
        int flag;

        // Check if a stop signal has been received from the tracker
        MPI_Iprobe(TRACKER_RANK, TAG_STOP, MPI_COMM_WORLD, &flag, &st);
        if (flag) {
            // Consume the stop signal and set the stop flag
            MPI_Recv(NULL, 0, MPI_CHAR, TRACKER_RANK, TAG_STOP, MPI_COMM_WORLD, &st);
            stop = 1;
            break;
        }

        // Check if there is a request for a segment from any peer
        MPI_Iprobe(MPI_ANY_SOURCE, TAG_REQUEST_SEGMENT, MPI_COMM_WORLD, &flag, &st);
        if (flag) {
            SegmentRequest req;
            // Receive the segment request
            MPI_Recv(&req, sizeof(req), MPI_BYTE, st.MPI_SOURCE, TAG_REQUEST_SEGMENT, MPI_COMM_WORLD, &st);

            char outHash[HASH_SIZE+1];
            memset(outHash, 0, sizeof(outHash));

            int found = 0; // Flag to indicate if the requested segment was found
            // Search through the files owned by this client
            for (int i = 0; i < cd->numOwned && !found; i++) {
                if (strcmp(cd->ownedFiles[i].fileName, req.fileName) == 0) { // Check if the file matches
                    if (req.segmentIndex >= 0 && req.segmentIndex < cd->ownedFiles[i].nr_segments) {
                        if (cd->ownedFiles[i].segmentExists[req.segmentIndex]) { // Check if the segment exists
                            strcpy(outHash, cd->ownedFiles[i].hashes[req.segmentIndex]); // Copy the segment hash
                            found = 1;
                        }
                    }
                }
            }

            if (!found) {
                // If the segment is not found, respond with "NONE"
                strcpy(outHash, "NONE");
            }

            // Send the response back to the requesting peer
            MPI_Send(outHash, HASH_SIZE+1, MPI_CHAR, st.MPI_SOURCE, TAG_SEND_SEGMENT, MPI_COMM_WORLD);
        }
    }
    return NULL;
}

void *download_thread_func(void *arg) {
    int rank = *(int *)arg;
    ClientData *cd = &clients[rank];

    // Wait for initialization acknowledgment from the tracker
    MPI_Status st;
    MPI_Recv(NULL, 0, MPI_CHAR, TRACKER_RANK, TAG_INIT_ACK, MPI_COMM_WORLD, &st);

    int downloadedSegmentsSoFar = 0; // Counter to track total downloaded segments

    // Iterate over all the files the client wants to download
    for (int w = 0; w < cd->numWanted; w++) {
        WantedFile *wf = &cd->wantedFiles[w];
        char *fname = wf->fileName;

        // Request the swarm information for this file from the tracker
        MPI_Send(fname, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TAG_SWARM_REQUEST, MPI_COMM_WORLD);

        SwarmResponse sr;
        MPI_Recv(&sr, sizeof(sr), MPI_BYTE, TRACKER_RANK, TAG_SWARM_RESPONSE, MPI_COMM_WORLD, &st);

        // Initialize segment tracking
        int numSegmentsWanted = sr.totalSegments;
        wf->nr_segmentsNeeded = numSegmentsWanted;
        memset(wf->downloadedSegmentExists, 0, sizeof(wf->downloadedSegmentExists));
        for (int s = 0; s < MAX_CHUNKS; s++) {
            wf->downloadedHashes[s][0] = '\0';
        }

        int gotSegments = 0; // Counter for successfully downloaded segments of this file
        int currentPeerIndex = 0; // Round-robin index to choose peers
        const int MAX_LOAD = 5; // Maximum load a peer can handle before being skipped

        // Download all required segments of the file.
        while (gotSegments < numSegmentsWanted) {
            // Select a peer using round-robin and filter by load
            int peerIndex = -1;
            for (int i = 0; i < sr.swarmSize; i++) {
                int candidateIndex = (currentPeerIndex + i) % sr.swarmSize;
                if (sr.info[candidateIndex].rank != rank && sr.info[candidateIndex].loadCount < MAX_LOAD) {
                    peerIndex = candidateIndex;
                    currentPeerIndex = candidateIndex + 1; // Update round-robin index
                    break;
                }
            }

            if (peerIndex == -1) {
                // No suitable peer available, continue checking
                continue;
            }

            int peerRank = sr.info[peerIndex].rank;

            // Find the first missing segment
            int segID = -1;
            for (int s = 0; s < numSegmentsWanted; s++) {
                if (!wf->downloadedSegmentExists[s]) {
                    segID = s;
                    break;
                }
            }

            if (segID == -1) break; // All segments are already downloaded

            // Send a request for the missing segment to the chosen peer
            SegmentRequest req;
            memset(&req, 0, sizeof(req));
            strcpy(req.fileName, fname);
            req.segmentIndex = segID;

            MPI_Send(&req, sizeof(req), MPI_BYTE, peerRank, TAG_REQUEST_SEGMENT, MPI_COMM_WORLD);

            char recvHash[HASH_SIZE + 1];
            MPI_Recv(recvHash, HASH_SIZE + 1, MPI_CHAR, peerRank, TAG_SEND_SEGMENT, MPI_COMM_WORLD, &st);

            // Verify the response and mark the segment as downloaded if valid
            if (strcmp(recvHash, "NONE") != 0) {
                strcpy(wf->downloadedHashes[segID], recvHash);
                wf->downloadedSegmentExists[segID] = 1;
                gotSegments++;
                downloadedSegmentsSoFar++;
            }

            // Refresh the swarm information after every 10 segments
            if (downloadedSegmentsSoFar % 10 == 0 && gotSegments < numSegmentsWanted) {
                MPI_Send(fname, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TAG_SWARM_REQUEST, MPI_COMM_WORLD);
                MPI_Recv(&sr, sizeof(sr), MPI_BYTE, TRACKER_RANK, TAG_SWARM_RESPONSE, MPI_COMM_WORLD, &st);
            }
        }

        // Notify the tracker that the file is completely downloaded
        MPI_Send(fname, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, TAG_FILE_COMPLETE, MPI_COMM_WORLD);

        // Save the downloaded file segments to a local file
        char outName[64];
        sprintf(outName, "client%d_%s", rank, fname);
        FILE *fout = fopen(outName, "w");
        if (fout) {
            for (int s = 0; s < numSegmentsWanted; s++) {
                fprintf(fout, "%s\n", wf->downloadedHashes[s]);
            }
            fclose(fout);
        }
    }

    // Notify the tracker that all downloads for this client are complete
    MPI_Send(NULL, 0, MPI_CHAR, TRACKER_RANK, TAG_ALL_COMPLETE, MPI_COMM_WORLD);
    cd->allComplete = 1; // Mark this client as finished

    return NULL;
}

void tracker(int numtasks, int rank) {
    TrackerData td;
    memset(&td, 0, sizeof(td));
    td.totalClients = numtasks - 1; // Total clients are all ranks except the tracker

    //Receive INIT messages from all clients
    for (int i = 1; i < numtasks; i++) {
        handleInit(&td, i);
    }

    // Send acknowledgment to all clients
    for (int i = 1; i < numtasks; i++) {
        MPI_Send(NULL, 0, MPI_CHAR, i, TAG_INIT_ACK, MPI_COMM_WORLD);
    }

    //Main tracker loop to handle requests
    int stop = 0;
    while (!stop) {
        MPI_Status st;
        MPI_Probe(MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, &st); // Check for incoming messages from any source

        int source = st.MPI_SOURCE;
        int tag = st.MPI_TAG;

        // Handle incoming requests based on the tag
        switch (tag) {
            case TAG_SWARM_REQUEST:
                handleSwarmRequest(&td, source); // Handle a request for swarm information from a client
                break;

            case TAG_FILE_COMPLETE:
                handleFileComplete(&td, source); // Mark a file as completely downloaded by the client
                break;

            case TAG_ALL_COMPLETE:
                handleAllComplete(&td, source, numtasks); // Handle a notification that a client has finished downloading all files
                stop = allFinished(&td, numtasks);
                break;
        }
    }
}
void peer(int numtasks, int rank) {
    ClientData *cd = &clients[rank];
    memset(cd, 0, sizeof(*cd));
    cd->rank = rank;

    parseInputFile(cd); // Parse the input file to populate the list of owned and wanted files
    sendInitMessage(cd); // Send an initialization message to the tracker with the list of owned files

    pthread_t download_thread; // Thread for downloading file segments
    pthread_t upload_thread; // Thread for uploading file segments
    void *status;
    int r;

    // Create the download thread
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Error creating the download thread\n");
        exit(-1);
    }

    // Create the upload thread
    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Error creating the upload thread\n");
        exit(-1);
    }

    // Wait for the download thread to finish
    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Error waiting for the download thread to finish\n");
        exit(-1);
    }

    // Wait for the upload thread to finish
    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Error waiting for the upload thread to finish\n");
        exit(-1);
    }
}

int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
