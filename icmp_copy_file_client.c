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
#define PANIC(msg)  { printf(msg); exit(EXIT_FAILURE); }

typedef struct {
    struct icmphdr header;
    char message[PACKETSIZE-sizeof(struct icmphdr)];
} packet_struct;
int init_seq = 300;

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

packet_struct create_init_packet(unsigned int file_size, char * fileName) {
  packet_struct packet;
  int pid = INITPID; // признак начала копирования
  bzero( & packet, sizeof(packet)); /*обнуляем содержимое */
  packet.header.type = ICMP_ECHO;
  packet.header.un.echo.id = pid;
  packet.header.un.echo.sequence = init_seq;
  unsigned char * b = (unsigned char * ) & file_size;
  //первые 4 байта содержат размер файла
  packet.message[0] = * b;
  packet.message[1] = * (b + 1);
  packet.message[2] = * (b + 2);
  packet.message[3] = * (b + 3);
  //5-25 байт имя файла
  for (int y = 4; y < 24; y++) {
    packet.message[y] = * (fileName + y - 4);
  }
  printf("DEBUG: sizeof(packet)=%d\n", sizeof(packet));
  packet.header.checksum = checksum( & packet, sizeof(packet));
  printf("DEBUG: checksum packet %d\n", packet.header.checksum);
  return packet;
}

packet_struct create_data_packet(char chunk_buffer[]) {
  packet_struct packet;
  int pid = INITPID + 1;
  bzero( & packet, sizeof(packet)); /*обнуляем содержимое */
  packet.header.type = ICMP_ECHO;
  packet.header.un.echo.id = pid;
  packet.header.un.echo.sequence = init_seq;
  memcpy(packet.message, chunk_buffer, CHUNKBYTE);
  packet.header.checksum = checksum( & packet, sizeof(packet));
  return packet;

}

int main(int argc, char * argv[]) {

  if (argc != 5) {
    PANIC("Incorrect number of arguments, usage: icmp_copy_file_client.bin -h<ip> -f<file>\n");
  }
  if (strcmp(argv[1], "-h") != 0 || strcmp(argv[3], "-f") != 0) {
    PANIC("Incorrect keys, usage: icmp_copy_file_client.bin -h<ip> -f<file>\n");
  }

  char * ip = argv[2];
  char * fileName = argv[4];
  printf("DEBUG: ip=%s, fileName=%s\n", ip, fileName);
  struct stat _fileStatbuff;
  unsigned int file_size = 0;
  int num_of_chunk;
  int last_chunk_seek;
  int chunk_counter = 1;
  int chunk_seek = 0;
  char chunk_buffer[CHUNKBYTE];
  bzero( & chunk_buffer, CHUNKBYTE);
  struct sockaddr_in addr;
  bzero( & addr, sizeof(addr));
  packet_struct packet;
  bzero( & packet, sizeof(packet));
  int sd;
  unsigned char buf[1024];

  FILE * fp = fopen(fileName, "r");
  if (fp == NULL) PANIC("Error with file\n");
  //S_ISREG(m) Test for a regular file.
  if ((fstat(fileno(fp), & _fileStatbuff) != 0) || (!S_ISREG(_fileStatbuff.st_mode))) {
    file_size = -1;
  } else {
    file_size = _fileStatbuff.st_size; /*общий размер в байтах */
  }
  printf("DEBUG: file_size=%d\n", file_size);
  if (file_size == -1) PANIC("Error with file processing\n");

  num_of_chunk = file_size / CHUNKBYTE;
  last_chunk_seek = file_size % CHUNKBYTE;
  if (last_chunk_seek > 0) {
    num_of_chunk++;
  }
  printf("DEBUG: last_chunk_seek %d\n", last_chunk_seek);
  printf("DEBUG: num_of_chunk %d\n", num_of_chunk);

  addr.sin_family = AF_INET;
  addr.sin_port = 0;
  packet = create_init_packet(file_size, fileName);
  if (inet_pton(AF_INET, ip, & (addr.sin_addr.s_addr)) != 1) {
    PANIC("Incorrect ip addr\n");
  }
  if ((sd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)) == -1) {
    PANIC("Error while creating a socket\n");
  }
  if (sendto(sd, & packet, sizeof(packet), 0, (struct sockaddr * ) & addr, sizeof(addr)) == -1) {
    PANIC("Error while sendto\n");
  }
  printf("DEBUG: Done\n");

  while (chunk_counter <= num_of_chunk) {

    fseek(fp, chunk_seek, SEEK_SET);
    fread(chunk_buffer, 1, CHUNKBYTE, fp);
    init_seq++;
    bzero( & packet, sizeof(packet));
    packet = create_data_packet(chunk_buffer);
    if (sendto(sd, & packet, sizeof(packet), 0, (struct sockaddr * ) & addr, sizeof(addr)) == -1) {
      PANIC("Error while sendto\n");
    }

    int bytes, len = sizeof(addr);
    bzero(buf, sizeof(buf));
    printf("Wait echo reply\n");
    while (1) {
      bytes = recvfrom(sd, buf, sizeof(buf), 0, (struct sockaddr * ) & addr, & len);
      if (bytes > 0) {
        struct iphdr * ip = buf;
        packet_struct * recv_packet = buf + ip -> ihl * 4;
        printf("DEBUG: calculated checksum =%d\n", checksum(recv_packet, sizeof( * recv_packet)));
        printf("DEBUG: (*recv_packet).header.type=%d\n", ( * recv_packet).header.type);
        printf("DEBUG: (*recv_packet).header.un.echo.id=%d\n", ( * recv_packet).header.un.echo.id);
        //убеждаемся что получили правильный эхо реплай
        if (( * recv_packet).header.un.echo.id == INITPID + 1 && ( * recv_packet).header.type == 0 && checksum(recv_packet, sizeof( * recv_packet)) == 0) {
          printf("DEBUG: reply ok\n");
          break;
        } else {
          printf("DEBUG: reply bad\n");
        }
      }
    }

    if (chunk_counter == num_of_chunk - 1 && last_chunk_seek > 0) {
      printf("last chunk \n");
      chunk_seek = chunk_seek + last_chunk_seek;
    } else {
      chunk_seek = chunk_seek + CHUNKBYTE;
    }
    chunk_counter++;
  }

  fclose(fp);

}
