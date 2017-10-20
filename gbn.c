#include "gbn.h"

/*--- Timeout ---*/
volatile sig_atomic_t timeoutFlag = 0;

/*the timeoutHandler for signal()*/
void timeoutHandler( int sig ) {
    timeoutFlag = 1;
    printf("-------Timeout Detected, Packets Subject to Lose-------\n");
}

/*timeout resetter*/
void timeoutReset() {
    timeoutFlag = 0;
}

/* global variable */
state_t sm = {SLOW, CLOSED, 2};
struct sockaddr * hostaddr, * clientaddr;
socklen_t host_len, client_len;

/* make the header of packets */
gbnhdr makeHeader(int type, uint8_t seqnum) {
    gbnhdr header;
    header.type = type;
    header.seqnum = seqnum;
    header.checksum = 0;
    
    return header;
}

/* Create a packet */
gbnhdr* packet(int type, uint8_t sequence_num, char *buffer, int data_length)
{
	gbnhdr * packet = malloc(sizeof(gbnhdr));
	packet->type = type;
	packet->seqnum = sequence_num;

	memcpy(packet->data, buffer, data_length);
	packet->len = data_length;

	packet->checksum = checksum((uint16_t *) buffer, data_length);

	return packet;
}


/* Check packet make sure it's valid */

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
    else if (timeoutFlag == 1 || length == -1) {
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

/* send packeet via gbn_send */
ssize_t gbn_send(int sockfd, const void *buf, size_t len, int flags){

	/* Hint: Check the data length field 'len'.
	 *       If it is > DATALEN, you will have to split the data
	 *       up into multiple packets - you don't have to worry
	 *       about getting more than N * DATALEN.
	 */

	int remainLen = len;
	int currLen = 0;
	int cumLen = 0;
	int count;
	int currMode = sm.mode;
	int currSeq = rand();
	sm.seqnum = currSeq;

	char * tmp_buf = (char *) malloc(DATALEN * sizeof(char));
	while (cumLen < len) {

		memset(tmp_buf, '\0', DATALEN);

		if (remainLen < DATALEN) {
			currLen = remainLen;
		}
		else {
			currLen = DATALEN;
		}

		count = 0;

		/* slow mode */
		if (currMode == SLOW) {
			gbnhdr * nextPacket;
			gbnhdr * recv_buf;

			memcpy(tmp_buf, buf + cumLen, currLen);

			while(count < 5 && sm.state != ACK_RCVD) {
				nextPacket = packet(DATA, currSeq, tmp_buf, currLen);

				if (sendto(sockfd, nextPacket, sizeof(*nextPacket), 0, clientaddr, client_len) == -1) {
					/* failed to send packet */
					continue;
				}

				/* sent packet, waiting for response */
				sm.mode = BYTE_SENT;
				alarm(TIMEOUT);

				recv_buf = malloc(sizeof(gbnhdr));
				int recv_size = recvfrom(sockfd, recv_buf, sizeof(gbnhdr), 0, hostaddr, &host_len);

				if(packetCheck(recv_buf, DATAACK, recv_size) == 0){
					sm.state = ACK_RCVD;
					currMode = FAST;
					cumLen += currLen;
					remainLen -= currLen;
					currSeq += 1;
					sm.seqnum = currSeq;
				} else {
					count += 1;
				}

			}

			/* fail to receive ACK after 5 attempts */
			if (sm.state == BYTE_SENT) {
				return -1;
			}

			/* receive ACK */
			sm.state = ESTABLISHED;
			free(nextPacket);
			free(recv_buf);


		/* fast mode */
		} else {
			memcpy(tmp_buf, buf + cumLen, currLen);
			sm.state = ESTABLISHED;

			int cumLen1 = cumLen;
			int len1  = currLen;
			int seqnum1 = currSeq;

			/* send first packet */
			gbnhdr * packet1 = packet(DATA, seqnum1, tmp_buf, len1);
			sendto(sockfd, packet1, sizeof(*packet1), 0, hostaddr, host_len);
			sm.state = BYTE_SENT;

			/* clear the buffer */
			memset(tmp_buf, '\0', DATALEN);

			/* send second only if there is still remaining buffer */
			int cumLen2;
			int len2;
			int seqnum2;
			if (cumLen + currLen < len) {
				currSeq += 1;
				sm.seqnum = currSeq;
				cumLen += currLen;
				cumLen2 = cumLen;
				seqnum2 = currSeq;

				if (remainLen >= DATALEN * 2) {
					len2 = DATALEN;
				}
				else {
					len2 = remainLen - DATALEN;
				}

				memcpy(tmp_buf, buf + cumLen, currLen);
				gbnhdr * packet2 = packet(DATA, currSeq, tmp_buf, currLen);

				sendto(sockfd, packet2, sizeof(*packet2), 0, hostaddr, host_len);

			}
			/* not enough remaining buffer */
			else {
				seqnum2 = currSeq;
				len2 = len1;
			}

			currSeq = seqnum1;
			gbnhdr * recv_buf;
			while(count < 5 && sm.state != ACK_RCVD){
				alarm(TIMEOUT);
				recv_buf = malloc(sizeof(gbnhdr));
				int rec_size = recvfrom(sockfd, recv_buf, sizeof * recv_buf, 0, hostaddr, &host_len);

				if(packetCheck(recv_buf, DATAACK, rec_size) == 0){

					/* receive ACK for second packet */
					if(recv_buf->seqnum == seqnum2) {
						sm.state = ACK_RCVD;
						cumLen += len2;
						remainLen -= len2;
						currSeq += 1;
						sm.seqnum = currSeq;
					}

					/* receive ACK for first packet */
					else if(recv_buf->seqnum == seqnum1) {
						remainLen -= len1;
						currSeq = seqnum2;
					}

					/* something else */
					else {
						count += 1;
					}
				} else {
					if (recv_buf->seqnum == seqnum1) {
						currMode = SLOW;
						cumLen = cumLen1;
						currSeq = seqnum1;
						break;
					}
					else if (recv_buf->seqnum == seqnum2 && currSeq == seqnum2){
						currMode = SLOW;
						cumLen = cumLen2;
						break;
					}
					else {
						count += 1;
					}
				}
				free(recv_buf);
			}

			/* start over again with first packet */
			if (count == 5) {
				currMode = SLOW;
				cumLen = cumLen1;
				currSeq = seqnum1;
			}
		}
	}
	sm.isEnd = 1;
	return remainLen;
}

/* receive packeet via gbn_recv */
ssize_t gbn_recv(int sockfd, void *buf, size_t len, int flags){

    gbnhdr * bufferedData = malloc(sizeof(gbnhdr));
    int recvSize = recvfrom(sockfd, bufferedData, sizeof(gbnhdr), 0, clientaddr, &client_len);
    
    int data_length = sizeof(*bufferedData->data);
    
    /* check whether data received is expected */
    if (bufferedData->type == DATA) {
        printf(" --- Data Received --- \n");
        
        gbnhdr ackowledge_header = makeHeader(DATAACK, bufferedData->seqnum);
        int sendack = sendto(sockfd, &ackowledge_header, sizeof(gbnhdr), 0, hostaddr, host_len);
        if (sendack == -1){
            return -1;
        }
        
        memcpy(buf, bufferedData->data, bufferedData->len);
        return bufferedData->len;
    }
    else {
        gbnhdr finackHeader = makeHeader(FINACK, 0);
        if(sendto(sockfd, &finackHeader, sizeof(gbnhdr), 0, hostaddr, host_len) != -1)
            /* to the end */
            return 0;
        else
            /* wrong data received */
            return -1;
    }
}

/* to close the socket */
int gbn_close(int sockfd){

	if (sockfd < 0) {
		return(-1);
	}
	else {
        /* check if actually is the end */
		if (sm.isEnd == 1) {
			gbnhdr FIN_Header = makeHeader(FIN, 0);
			if (sendto(sockfd, &FIN_Header, sizeof(gbnhdr), 0, clientaddr, client_len) == -1) {
				return -1;
			}
		}
		else {
			gbnhdr FINACK_Header = makeHeader(FINACK, 0);
			if (sendto(sockfd, &FINACK_Header, sizeof(gbnhdr), 0, hostaddr, host_len) == -1) {
				return -1;
			}
			close(sockfd);
		}
	}

	return 0;
}

/* iniate the connection via gbn_connect */
int gbn_connect(int sockfd, const struct sockaddr *server, socklen_t socklen){

    clientaddr = server;
    client_len = socklen;
    
    if (sockfd < 0)
        return -1;
    
    /* SYN_SENT Implementation */
    gbnhdr SYN_Header = makeHeader(SYN, 0);
    sm.state = SYN_SENT;
    sm.isEnd = 0;
    int count = 0;
    
    
    while (count < 5) {
        int sendtoReturn = sendto(sockfd, &SYN_Header, sizeof SYN_Header, 0, server, socklen);
        
        if (sendtoReturn == -1) { /* Send error, try again*/
            count++;
            continue;
        }
        
        alarm(TIMEOUT);
        
        gbnhdr * recvBuf = malloc(sizeof(gbnhdr));
        int recvSize = recvfrom(sockfd, recvBuf, sizeof(gbnhdr), 0, hostaddr, &host_len);
        
        /* check timeout */
        if (timeoutFlag == 1 || recvSize == -1) {
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

/* to listen activities on socket via gbn_listen */
int gbn_listen(int sockfd, int backlog){

    printf("---Listening...---\n");
    gbnhdr * buff = malloc(sizeof(gbnhdr));
    int ackowledge_packet = recvfrom(sockfd, buff, sizeof(gbnhdr), 0, hostaddr, &host_len);
    /* Found packet */
    
    return packetCheck(buff, SYN, ackowledge_packet);

}

/* to bind a socket */
int gbn_bind(int sockfd, const struct sockaddr *server, socklen_t socklen){

    hostaddr = server;
    host_len = socklen;

    return bind(sockfd, server, socklen);
}	

/* socket set up */
int gbn_socket(int domain, int type, int protocol){
		
	/*----- Randomizing the seed. This is used by the rand() function -----*/
	srand((unsigned)time(0));
    
    signal(SIGALRM, timeoutHandler);

	return socket(domain, type, protocol);
}

/* to accept incoming connections */
int gbn_accept(int sockfd, struct sockaddr *client, socklen_t *socklen){

	if (sockfd < 0) {
		return(-1);
	}
	else {
        /* check validity of SYNACK_Header */
		gbnhdr SYNACK_Header = makeHeader(SYNACK, 0);
		if (sendto(sockfd, &SYNACK_Header, sizeof(SYNACK_Header), 0, hostaddr, host_len) == -1) {
			return -1;
		}
	}
	return sockfd;
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
