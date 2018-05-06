#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <linux/netlink.h>
#include <unistd.h>

#include "nvmveri_socket.h"

#define MYPROTO NETLINK_USERSOCK
#define MYMGRP 21
#define MAX_MSG_LENGTH 65536

// nvmveri
int termSignal;
int getResultSignal;

// netlink
struct sockaddr_nl local_addr, remote_addr;
struct nlmsghdr *nlh = NULL;
struct iovec iov;
int sock;
struct msghdr msg;
char* buffer;

int open_netlink()
{
    int group = MYMGRP;
	
    sock = socket(AF_NETLINK, SOCK_RAW, MYPROTO);
    if (sock < 0) {
        printf("sock < 0.\n");
        return -1;
    }

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.nl_family = AF_NETLINK;
    local_addr.nl_pid = getpid();

    if (bind(sock, (struct sockaddr *) &local_addr, sizeof(local_addr)) < 0) {
        printf("bind < 0.\n");
        return -1;
    }

	if (setsockopt(sock, 270, NETLINK_ADD_MEMBERSHIP, &group, sizeof(group)) < 0) {
			printf("setsockopt < 0\n");
		return -1;
	}
    
	memset(&remote_addr, 0, sizeof(remote_addr));
    remote_addr.nl_family = AF_NETLINK;
    remote_addr.nl_pid = 0; /* For Linux Kernel */
    remote_addr.nl_groups = 0;

	// initialize netlink buffer
	/*
	nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_MSG_LENGTH));
	memset(nlh, 0, NLMSG_SPACE(MAX_MSG_LENGTH));
    
	nlh->nlmsg_len = NLMSG_SPACE(MAX_MSG_LENGTH);
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 0;
	*/
	
	buffer = (char*) malloc(sizeof(char) * MAX_MSG_LENGTH);
	memset(buffer, 0, MAX_MSG_LENGTH);

    iov.iov_base = (void *) buffer;
    iov.iov_len = MAX_MSG_LENGTH;
    msg.msg_name = (void *) &(remote_addr);
    msg.msg_namelen = sizeof(remote_addr);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
	
    return sock;
}

int close_netlink() {
	free(buffer);
	close(sock);
	free(nlh);
}


int read_event(struct Vector *MetadataVectorPtr)
{
    int msg_size;
    int pos = 0;
	int last_packet;
	int i;
    char* data;
	int num_metadata;
    
	printf("Ok, listening.\n");
	memset(buffer, 0, MAX_MSG_LENGTH);
    msg_size = recvmsg(sock, &msg, 0);

    if (msg_size < 0) return -1;

	data = (char*) NLMSG_DATA((struct nlmsghdr *) buffer);
    // Read tx flag
    last_packet = *(int*)(data + pos);
	printf("last_packet=%d\n", last_packet);
    pos += sizeof(int);
    /* Read number of metadata packets
     * num_metadata < 0  -  terminate
     * num_metadata = 0  -  getVeri
     * otherwise, regular packets */
	num_metadata = *(int *)(data + pos);
	printf("num_metadata=%d\n", num_metadata);
    if (num_metadata < 0) {
        termSignal = 1;
        return 0;
    } else if (num_metadata == 0) {
        getResultSignal = 1;
        return 0;
    }
    pos += sizeof(int);

    //Read each packet and push to metadata vector
    for (i = 0; i < num_metadata; ++i) {
		struct Metadata* metadata =  (struct Metadata*)malloc(sizeof(struct Metadata));
		memcpy(metadata, (struct Metadata*)(data + pos), sizeof(struct Metadata));
		printf("metadata type=%d, addr=%llu, size=%lu\n", 
				(int)metadata->type, (addr_t)metadata->assign.addr, metadata->assign.size);
        pushVector(MetadataVectorPtr, metadata);
        pos += sizeof(struct Metadata);
    }
	
	return last_packet;
}


void send_ack(char* ack_msg) 
{	
    memset(nlh, 0, NLMSG_SPACE(MAX_MSG_LENGTH));
    strcpy((char*)NLMSG_DATA(nlh), ack_msg);

    sendmsg(sock, &msg, 0);
}



int main(int argc, char *argv[])
{
    int nls;
    int last_packet = 1;
    struct Vector* MetadataVectorPtr;
    

    // Open connection
    nls = open_netlink();

	printf("Metadata size=%lu\n", sizeof(struct Metadata));

    if (nls < 0) return nls;

    while (1) {
		printf("New iteration\n");
		termSignal = 0;
		getResultSignal = 0;
        
		if (last_packet) {
            MetadataVectorPtr = (struct Vector*)malloc(sizeof(struct Vector));
			initVector(MetadataVectorPtr);
		}

        last_packet = read_event(MetadataVectorPtr);
		if (last_packet < 0) {
			printf("Receive error\n");
			return 0;
		}

        // Check termination signal
        if (termSignal) {
			printf("Terminate\n");
			break;
		} else if (getResultSignal) {
			printf("get result\n");
			//send_ack("VERI_COMPLETE");
		} else {
			//send_ack("RECEIVED");
		}
	 	    // Start verification once all packets have been received
        if (last_packet) {
			printf("last one\n");
            //execVeri(MetadataVectorPtr);
        }
    }

	close_netlink();

    return 0;
}