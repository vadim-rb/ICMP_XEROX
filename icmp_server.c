/*
Disable system echo reply
Edit /etc/sysctl.conf
Add the following line to your /etc/sysctl.conf:

net.ipv4.icmp_echo_ignore_all=1
Then:

sysctl -p

 The data received in the echo message must be returned in the echo reply message.
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#include <netinet/ip_icmp.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

#define CHUNKBYTE 30
#define PACKETSIZE 64
#define INITPID 1934
#define PANIC(msg) {  printf(msg); exit(EXIT_FAILURE);}

typedef struct {
  struct icmphdr header;
  char message[PACKETSIZE - sizeof(struct icmphdr)];
}
packet_struct;


/*--------------------------------------------------------------------*/
/*--- checksum - standard 1s complement checksum                   ---*/
/*--------------------------------------------------------------------*/
unsigned short checksum(void * b, int len) {
  unsigned short * buf = b;
  unsigned int sum = 0;
  unsigned short result;

  for (sum = 0; len > 1; len -= 2)
    sum += * buf++;
  if (len == 1)
    sum += * (unsigned char * ) buf;
  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  result = ~sum;
  return result;
}

void send_echo_reply(char * ip, int seq, packet_struct p, int pid) {
  printf("DEBUG: send echo reply\n");
  packet_struct packet;
  struct sockaddr_in addr;
  bzero( & addr, sizeof(addr));
  int sd;
  //int pid = INITPID;
  bzero( & packet, sizeof(packet));

  packet.header.type = ICMP_ECHOREPLY; //0 
  packet.header.un.echo.id = pid;
  packet.header.un.echo.sequence = seq;

  // The data received in the echo message must be returned in the echo reply message.
  memcpy(packet.message, p.message, PACKETSIZE - sizeof(struct icmphdr));

  packet.header.checksum = checksum( & packet, sizeof(packet));

  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  if ((sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1) {
    PANIC("Error while creating a socket\n");
  }
  if (sendto(sd, & packet, sizeof(packet), 0, (struct sockaddr * ) & addr, sizeof(addr)) == -1) {
    PANIC("Error while sendto\n");
  }
}

int wait_init_packet(void * buf, int bytes, unsigned int *fs, char fileName[20]) {
  struct iphdr * ip = buf;
  packet_struct * packet = buf + ip -> ihl * 4;
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, & ip -> saddr, client_ip, sizeof(client_ip));
  printf("DEBUG: src addres %s\n", client_ip);
  printf("DEBUG: calculated checksum =%d\n", checksum(packet, sizeof( * packet)));
  if (checksum(packet, sizeof( * packet)) != 0) {
    printf("DEBUG: checksum error\n");
    return -1;
  }
  printf("DEBUG: packet.header.un.echo.id=%d\n", ( * packet).header.un.echo.id);
  printf("DEBUG: packet.header.un.echo.sequence=%d\n", ( * packet).header.un.echo.sequence);
  printf("DEBUG: packet.header.type=%d\n", ( * packet).header.type);
  if (( * packet).header.un.echo.id == INITPID && ( * packet).header.type==ICMP_ECHO) {
    printf("DEBUG: init packet catched\n");
    *fs = * (unsigned int * ) & ( * packet).message;
    printf("DEBUG: filesize fs=%d\n", *fs);
    if (*fs == 0) {
      printf("DEBUG: fs=0 error\n");
      return -1;
    }
    for (int y = 4; y < 24; y++) {
      fileName[y - 4] = ( * packet).message[y];
    }
    printf("DEBUG: fileName=%s\n", fileName);

    send_echo_reply(client_ip, ( * packet).header.un.echo.sequence, ( * packet), ( * packet).header.un.echo.id);
    return 0;
  }else{
    return -1;
  }
}

int wait_data_packet(void * buf, int bytes, char buffer[]) {
  struct iphdr * ip = buf;
  packet_struct * packet = buf + ip -> ihl * 4;
  char client_ip[INET_ADDRSTRLEN];
  inet_ntop(AF_INET, & ip -> saddr, client_ip, sizeof(client_ip));
  if (checksum(packet, sizeof( * packet)) != 0) {
    printf("DEBUG: data_packet checksum error\n");
    return -1;
  }
  if (( * packet).header.un.echo.id == INITPID+1 && ( * packet).header.type==ICMP_ECHO) {
    printf("DEBUG: data packet catched %d\n",(*packet).header.un.echo.sequence);
    memcpy(buffer,&( * packet).message,CHUNKBYTE);

    send_echo_reply(client_ip, ( * packet).header.un.echo.sequence, ( * packet), ( * packet).header.un.echo.id);
    return 0;
  }else{
    return -1;
  }
}

int main(void)

{
  int sd;
  struct sockaddr_in addr;
  unsigned char buf[1024];
  unsigned int file_size_copy;
  char fileName_copy[20];
  FILE * file_copy;
  int num_of_chunk;
  int last_chunk_seek;
  int chunk_counter = 1;
  int chunk_seek = 0;
  char buffer[CHUNKBYTE];
  bzero(&buffer, CHUNKBYTE);


  sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
  if (sd < 0) {
    perror("Error while create socket\n");
    exit(EXIT_FAILURE);
  }
  while (1) {
    printf("DEBUG: wait init packet\n");
    int bytes, len = sizeof(addr);

    bzero(buf, sizeof(buf));
    bytes = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr * ) & addr, & len);
    if (bytes > 0) {
      if (wait_init_packet(buf, bytes, &file_size_copy, fileName_copy) == 0) {
	  file_copy = fopen(fileName_copy, "w");
	  printf("DEBUG: file_size_copy=%d\n",file_size_copy);

	  printf("DEBUG: fileName_copy=%s\n",fileName_copy);
          num_of_chunk = file_size_copy / CHUNKBYTE;
          last_chunk_seek = file_size_copy % CHUNKBYTE;
          if (last_chunk_seek > 0) {
            num_of_chunk++;
          }
          printf("DEBUG: last_chunk_seek %d\n", last_chunk_seek);
          printf("DEBUG: num_of_chunk %d\n", num_of_chunk);
          while(1){

printf("DEBUG: ---------chunk_counter=%d\n",chunk_counter);
printf("DEBUG: ---------num_of_chunk=%d\n",num_of_chunk);
	    if(chunk_counter > num_of_chunk){break;}
	    bzero(buf, sizeof(buf));
            bytes = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr * ) & addr, & len);
    	    if (bytes > 0) {
              if (wait_data_packet(buf, bytes,buffer)==0){
printf("DEBUG: ---------chunk_seek=%d\n",chunk_seek);
printf("DEBUG: ---------buffer=%s\n",buffer);
                fseek(file_copy, chunk_seek, SEEK_SET);
                fwrite(buffer, 1, CHUNKBYTE, file_copy);
printf("DEBUG: fwrite\n");
		if (chunk_counter == num_of_chunk - 1 && last_chunk_seek > 0) {
		  printf("DEBUG: last chunk \n");
		  chunk_seek = chunk_seek + last_chunk_seek;
		} else {
		  chunk_seek = chunk_seek + CHUNKBYTE;
		}
		chunk_counter++;
	      }
            }

	  }
	fclose(file_copy);
      } else {
        printf("DEBUG:wait_init_packet error\n");
      }
    } else
      perror("Error while recvfrom\n");
  }
  exit(0);
}
