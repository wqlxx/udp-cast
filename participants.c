#include "socklib.h"
#include "log.h"
#include "util.h"
#include "participants.h"
#include "udpcast.h"
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/if_ether.h>
#ifdef USE_SYSLOG
#include <syslog.h>
#endif

struct  participantsDb {
    int nrParticipants;
    
    struct clientDesc {
	struct sockaddr_in addr;
        int sock;                       /*  add by wangq */
	int used;
	int capabilities;
	unsigned int rcvbuf;
    } clientTable[MAX_CLIENTS];
};

int addParticipant(participantsDb_t,
		   struct sockaddr_in *addr, 
		   int capabilities, 
		   unsigned int rcvbuf,
		   int pointopoint, int sock);

int isParticipantValid(struct participantsDb *db, int i) {
    return db->clientTable[i].used;
}

int removeParticipant(struct participantsDb *db, int i) {
    if(db->clientTable[i].used) {
	char ipBuffer[16];	
	flprintf("Disconnecting #%d (%s)\n", i, 
		 getIpString(&db->clientTable[i].addr, ipBuffer));
#ifdef USE_SYSLOG
	syslog(LOG_INFO, "Disconnecting #%d (%s)\n", i,
			getIpString(&db->clientTable[i].addr, ipBuffer));
#endif
	db->clientTable[i].used = 0;
	db->nrParticipants--;
    }
    return 0;
}

int lookupParticipant(struct participantsDb *db, struct sockaddr_in *addr) {
    int i;
    for (i=0; i < MAX_CLIENTS; i++) {
	if (db->clientTable[i].used && 
	    ipIsEqual(&db->clientTable[i].addr, addr)) {
	    return i;
	}
    }
    return -1;
}

int nrParticipants(participantsDb_t db) {
    return db->nrParticipants;
}

/************************************/
#define FILE_IP_ADDR "/tmp/ip_addr.txt"
/*modied by wangq 20140605*/
struct ip_list {
	char ip[16];
	struct ip_list *next;
};

struct ip_list ip_head = { .next =  NULL};
struct ip_list *tmp_ip_p = &ip_head;

static int rcd_getpeermac( int sockfd, char *buf )
{
	int ret =0;
	struct arpreq arpreq;
	struct sockaddr_in dstadd_in;
        socklen_t  len = sizeof( struct sockaddr_in );

        memset( &arpreq, 0, sizeof( struct arpreq ));
	memset( &dstadd_in, 0, sizeof( struct sockaddr_in ));
	if( getpeername( sockfd, (struct sockaddr*)&dstadd_in, &len ) < 0 ) {
		perror("getpeername()");
        } else {
		memcpy( &arpreq.arp_pa, &dstadd_in, sizeof( struct sockaddr_in ));
		strcpy(arpreq.arp_dev, "br0");
		arpreq.arp_pa.sa_family = AF_INET;
		arpreq.arp_ha.sa_family = AF_UNSPEC;
		if( ioctl( sockfd, SIOCGARP, &arpreq ) < 0 ) {
		     perror("ioctl SIOCGARP");
		} else {
                    unsigned char* ptr = (unsigned char *)arpreq.arp_ha.sa_data;
                    ret = sprintf(buf, "%02x%02x%02x%02x%02x%02x", *ptr, *(ptr+1), *(ptr+2), *(ptr+3), *(ptr+4), *(ptr+5));
		}
	}
	return ret;
}

static int
rcd_write_ip(char *s, int sock)
{        
	FILE* fp;
        char buf[20];

        if((fp = fopen(FILE_IP_ADDR, "a+")) == NULL){
		return -1; 
	}
        rcd_getpeermac(sock, buf);    

        fprintf(fp, "%s\n", buf);
	rewind(fp);
	fclose(fp);
	return 0;
}

static int
rcd_check_ip(char *s, int sock)
{
        struct ip_list *tmp;
        struct ip_list *tmp1;
        int ret;

        tmp1 = ip_head.next;
        tmp = &ip_head;
        while(tmp1)
        {
                ret = strcmp(tmp1->ip, s);
                if(ret == 0){
                        return 0;
                }
                tmp = tmp->next;
                tmp1 = tmp1->next;
        }

        if( tmp1 == NULL){
                tmp1 = (struct ip_list *)malloc(sizeof(struct ip_list));
                strcpy(tmp1->ip, s);
                tmp1->next = NULL;
                tmp->next = tmp1;
                rcd_write_ip(s, sock);
        }

        return 0;

}
/* end of modify 20140605*/
/************************************/

int addParticipant(participantsDb_t db,
		   struct sockaddr_in *addr, 
		   int capabilities,
		   unsigned int rcvbuf,
		   int pointopoint, int sock) {
    int i;

    if((i = lookupParticipant(db, addr)) >= 0)
	return i;

    for (i=0; i < MAX_CLIENTS; i++) {
	if (!db->clientTable[i].used) {
	    char ipBuffer[16];
	    db->clientTable[i].addr = *addr;
	    db->clientTable[i].used = 1;
	    db->clientTable[i].capabilities = capabilities;
	    db->clientTable[i].rcvbuf = rcvbuf;
            db->clientTable[i].sock = sock;
	    db->nrParticipants++;

	    fprintf(stderr, "New connection from %s  (#%d) %08x\n", 
		    getIpString(addr, ipBuffer), i, capabilities);
	    //rcd_write_ip(ipBuffer);
	    rcd_check_ip(ipBuffer, sock);//modied by wangq 20140605
#ifdef USE_SYSLOG
	    syslog(LOG_INFO, "New connection from %s  (#%d)\n",
			    getIpString(addr, ipBuffer), i);
#endif
	    return i;
	} else if(pointopoint)
	    return -1;
    }

    return -1; /* no space left in participant's table */
}

participantsDb_t makeParticipantsDb(void)
{
    return MALLOC(struct participantsDb);
}

int getParticipantCapabilities(participantsDb_t db, int i)
{
    return db->clientTable[i].capabilities;
}

unsigned int getParticipantRcvBuf(participantsDb_t db, int i)
{
    return db->clientTable[i].rcvbuf;
}

struct sockaddr_in *getParticipantIp(participantsDb_t db, int i)
{
    return &db->clientTable[i].addr;
}
    
void printNotSet(participantsDb_t db, char *d)
{
    int first=1;
    int i;
    flprintf("[");
    for (i=0; i < MAX_CLIENTS; i++) {
	if (db->clientTable[i].used) {
	    if(!BIT_ISSET(i, d)) {
		if(!first)
		    flprintf(",");
		first=0;
		flprintf("%d", i);
	    }
	}
    }
    flprintf("]");
}


void printSet(participantsDb_t db, char *d)
{
    int first=1;
    int i;
    flprintf("[");
    for (i=0; i < MAX_CLIENTS; i++) {
	if (db->clientTable[i].used) {
	    if(BIT_ISSET(i, d)) {
		if(!first)
		    flprintf(",");
		first=0;
		flprintf("%d", i);
	    }
	}
    }
    flprintf("]");
}
