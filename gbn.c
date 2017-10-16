#include "gbn.h"

/*--- Timeout ---*/
volatile sig_atomic_t timeoutFlag = false;

/*the timeoutHandler for signal()*/
void timeoutHandler( int sig )
{
    timeoutFlag = true;
    printf("-------Timeout Detected, Packets Subject to Lose-------\n");
}

/*timeout resetter*/
void timeoutReset()
{
    timeoutFlag = false;
}

state_t sm = {SLOW, CLOSED, 2};

struct sockaddr * hostaddr, * clientaddr;
socklen_t host_len, client_len;

gbnhdr makeHeader(int type, uint8_t seqnum)
{
    gbnhdr header;
    header.type = type;
    header.seqnum = seqnum;
    header.checksum = 0;
    
    return header;
}

int packetCheck(gbnhdr * packet, int type, int length) {
    
    /* Check SYN */
    if (packet->type == SYN) {
        return 0;
    }
    /* Check type */
    else if (packet->type != type) {
        printf("Wrong packet type");
        return -1;
    }
    /* Check timeout*/
    else if (timeoutFlag == true || length == -1) {
            timeoutReset();
            return -1;
    }
    /* Check seqnum*/
    else if (packet->seqnum < sm.seqnum){
        printf("Wrong seqnum");
        return -1;
    }
    
    printf("----Packet Received-----");
    return 0;
}


uint16_t checksum(uint16_t *buf, int nwords)
{
	uint32_t sum;

	for (sum = 0; nwords > 0; nwords--)
		sum += *buf++;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);
	return ~sum;
}

ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags){
	
	/* TODO: Your code here. */

	/* Hint: Check the data length field 'len'.
	 *       If it is > DATALEN, you will have to split the data
	 *       up into multiple packets - you don't have to worry
	 *       about getting more than N * DATALEN.
	 */

	return(-1);
}

ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){

	/* TODO: Your code here. */

	return(-1);
}

int gbn_close(int sockfd){

	/* TODO: Your code here. */

	return(-1);
}

int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

    clientaddr = server;
    client_len = socklen;
    
    if (sockfd < 0)
        return -1;
    
    gbnhdr SYN_Header = makeHeader(SYN, 0);
    sm.state = SYN_SENT;
    sm.isEnd = 0;
    int count = 0;
    printf("----SYN_SENT Implementation-----");
    
    while (count < 5) {
        int sendtoReturn = sendto(sockfd, &SYN_Header, sizeof SYN_Header, 0, server, socklen);
        
        if (sendtoReturn == -1) { /* Send error, try again*/
            count++;
            continue;
        }
        
        alarm(TIMEOUT);
        
        gbnhdr * recvBuf = malloc(sizeof(gbnhdr));
        int recvSize = recvfrom(sockfd, recvBuf, sizeof(gbnhdr), 0, hostaddr, &host_len);
        
        if (timeoutFlag == true || recvSize == -1) {
            count++;
            timeoutReset();
        }
        else { /* Received a packet*/
            if (recvBuf->type == SYNACK) { /* Check to see if we got the right response*/
                sm.state = ESTABLISHED;
                printf("----------Connection Established Successfully--------------\n");
                return 0;
            }
            
            count++;
        }
    }
    
    sm.state = CLOSED;
	return(-1);
}

int gbn_listen(int sockfd, int backlog){

    printf("---Listening...---\n");
    gbnhdr * buff = malloc(sizeof(gbnhdr));
    int ackowledge_packet = recvfrom(sockfd, buff, sizeof(gbnhdr), 0, hostaddr, &host_len);
    /* Found packet */
    
    return packetCheck(buff, SYN, ackowledge_packet);

}

int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen){

    hostaddr = server;
    host_len = socklen;

    return bind(sockfd, server, socklen);
}	

int gbn_socket(int domain, int type, int protocol){
		
	/*----- Randomizing the seed. This is used by the rand() function -----*/
	srand((unsigned)time(0));
    
    signal(SIGALRM, timeoutHandler);

	return socket(domain, type, protocol);
}

int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen){

	/* TODO: Your code here. */

	return(-1);
}

ssize_t maybe_sendto(int  s, const void *buf, size_t len, int flags, \
                     const struct sockaddr *to, socklen_t tolen){

	char *buffer = malloc(len);
	memcpy(buffer, buf, len);
	
	
	/*----- Packet not lost -----*/
	if (rand() > LOSS_PROB*RAND_MAX){
		/*----- Packet corrupted -----*/
		if (rand() < CORR_PROB*RAND_MAX){
			
			/*----- Selecting a random byte inside the packet -----*/
			int index = (int)((len-1)*rand()/(RAND_MAX + 1.0));

			/*----- Inverting a bit -----*/
			char c = buffer[index];
			if (c & 0x01)
				c &= 0xFE;
			else
				c |= 0x01;
			buffer[index] = c;
		}

		/*----- Sending the packet -----*/
		int retval = sendto(s, buffer, len, flags, to, tolen);
		free(buffer);
		return retval;
	}
	/*----- Packet lost -----*/
	else
		return(len);  /* Simulate a success */
}
