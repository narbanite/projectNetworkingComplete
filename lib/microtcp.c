/*
 * microtcp, a lightweight implementation of TCP for teaching,
 * and academic purposes.
 *
 * Copyright (C) 2015-2017  Manolis Surligas <surligas@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "microtcp.h"
#include "../utils/crc32.h"
#include <stdio.h>
#include <errno.h>
#include <time.h>

/*our global vars*/
const struct sockaddr *cl, *sr;
socklen_t cl_len = sizeof(struct sockaddr_in), sr_len = sizeof(struct sockaddr);

microtcp_sock_t
microtcp_socket (int domain, int type, int protocol)
{
  microtcp_sock_t mysocket;
  int sock;
  if ((sock = socket(domain, type, protocol)) == -1)
  {
    printf(" SOCKET COULD NOT BE OPENED \n");
    exit(EXIT_FAILURE);
  }

  memset(&mysocket, 0, sizeof(mysocket));

  mysocket.sd = sock;
  mysocket.state = INIT;

  // mysocket.sd = sock;
  // mysocket.state = INIT;
  // mysocket.init_win_size = MICROTCP_WIN_SIZE;
  // mysocket.curr_win_size = mysocket.init_win_size;
  // mysocket.recvbuf = malloc(MICROTCP_RECVBUF_LEN);
  // mysocket.buf_fill_level = 0;
  // mysocket.cwnd = MICROTCP_INIT_CWND;
  // mysocket.ssthresh = MICROTCP_INIT_SSTHRESH;
  // mysocket.seq_number = 0; /**< Keep the state of the sequence number */
  // mysocket.ack_number = 0; /**< Keep the state of the ack number */
  // memset(&mysocket.packets_send, 0, sizeof(mysocket.packets_send));
  // memset(&mysocket.packets_received, 0, sizeof(mysocket.packets_received));
  // memset(&mysocket.packets_lost, 0, sizeof(mysocket.packets_lost));
  // memset(&mysocket.bytes_send, 0, sizeof(mysocket.bytes_send));
  // memset(&mysocket.bytes_received, 0, sizeof(mysocket.bytes_received));
  // memset(&mysocket.bytes_lost, 0, sizeof(mysocket.bytes_lost));
  // //memset(&mysocket.myaddr, 0, sizeof(struct sockaddr));
  // //memset(&mysocket.destaddr, 0, sizeof(struct sockaddr));

  return mysocket;
}

int
microtcp_bind (microtcp_sock_t *socket, const struct sockaddr *address,
               socklen_t address_len)
{
  if (address == NULL || socket == NULL)
  {
    printf("Invalid input in microtcp_bind\n");
    return -1;
  }

  if (bind(socket->sd, address, address_len) == -1)
  {
    printf("Error in binding, closing the socket.\n");
    return -1;
  }

  /*here we know the address of the server so we can initialize the global variable sr and the field on the server's socket*/
  sr = address;

  socket->state = LISTEN;
  //socket->myaddr = address;

  return 0; /*the binding was successful*/
}

int
microtcp_connect (microtcp_sock_t *socket, const struct sockaddr *address,
                  socklen_t address_len)
{
  srand(time(NULL));   /*to create a random sequence number on each run*/

  socket->destaddr = address; /*here client knows server's adress which is its destination address*/

  message_t *mssg = malloc(sizeof(mssg));
  void *rcv = malloc(MICROTCP_RECVBUF_LEN);

  memset(mssg, 0, sizeof(message_t));
  
  /*make message*/
  mssg->header.seq_number = (uint32_t)(rand() % 10);
  socket->seq_number = mssg->header.seq_number;
  mssg->header.control = mssg->header.control | SYN;
  mssg->header.window = MICROTCP_WIN_SIZE;
  socket->init_win_size = MICROTCP_WIN_SIZE;

  printf("Sending SYN, seq=%d, win=%d\n", mssg->header.seq_number, mssg->header.window);

  if (sendto(socket->sd, (const void *)mssg, MICROTCP_RECVBUF_LEN, 0, address, address_len) == -1)
  {
    printf("Error in sending the message from socket <%d>\n", socket->sd);
    return -1;
  }

  if (recvfrom(socket->sd, (void *restrict)rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&address_len) == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    return -1;
  }

  memcpy(mssg, rcv, sizeof(message_t));

  sleep(1);
  printf("Received ??, seq=%d, ack=%d\n", mssg->header.seq_number, mssg->header.ack_number);

  if (mssg->header.control != (SYN | ACK)) return -1;
  
  sleep(1);
  printf("Received SYN ACK with win=%d\n", mssg->header.window);

  socket->curr_win_size = mssg->header.window; /*fixing curr_win_size of client according to what the server sent to him*/

  /*making next message*/
  mssg->header.ack_number = mssg->header.seq_number + 1;
  socket->ack_number = mssg->header.ack_number;
  mssg->header.seq_number = socket->seq_number + 1;
  socket->seq_number = socket->seq_number + 1;
  mssg->header.window = MICROTCP_WIN_SIZE;
  mssg->header.control = mssg->header.control & (~(SYN)); /*we are "subtracting" the SYN flag*/

  sleep(1);
  printf("Sending ACK, seq=%d, ack=%d\n", mssg->header.seq_number, mssg->header.ack_number);
  if (sendto(socket->sd, mssg, MICROTCP_RECVBUF_LEN, 0, address, address_len) == -1)
  {
    printf("Error in sending the message from socket <%d>\n", socket->sd);
    return -1;
  }

  socket->state = ESTABLISHED;

  /*allocate memory for recvbuf and initialize the window values accordingly*/
  socket->recvbuf = malloc(MICROTCP_RECVBUF_LEN);
  socket->cwnd = MICROTCP_INIT_CWND;
  socket->ssthresh = MICROTCP_INIT_SSTHRESH;

  return 0; /*the connection was successful*/
}

int
microtcp_accept (microtcp_sock_t *socket, struct sockaddr *address,
                 socklen_t address_len)
{
  srand(time(NULL));

  message_t *mssg = malloc(sizeof(message_t));
  void *rcv = malloc(MICROTCP_RECVBUF_LEN);
  memset(rcv, 0, sizeof(rcv));

  memset(mssg, 0, sizeof(mssg));

  cl = address; /*here the server knows the address of the client, so we can initialize the global variable cl*/
  socket->destaddr = address; /*which is also its destinaton address*/

  if (recvfrom(socket->sd, (void *restrict)rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&address_len) == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    fprintf(stderr, "Error: %s\n", strerror(errno));
    return -1;
  }

  memcpy(mssg, rcv, sizeof(message_t));

  sleep(1);
  printf("Received ??, seq=%d\n", mssg->header.seq_number);

  if (mssg->header.control != (SYN)) return -1;
  
  sleep(1);
  printf("Received SYN with win=%d\n", mssg->header.window);

  /*make message*/
  mssg->header.ack_number = mssg->header.seq_number + 1;
  socket->ack_number = mssg->header.ack_number;
  mssg->header.seq_number = (uint32_t)(rand() % 10);
  socket->seq_number = mssg->header.seq_number;
  mssg->header.control = mssg->header.control | ACK; /*making the SYN ACK flag only with the |ACK because the header has already the SYN in it*/
  mssg->header.window = MICROTCP_WIN_SIZE;
  socket->init_win_size = mssg->header.window;

  sleep(1);
  printf("Sending SYN ACK, seq=%d, ack=%d, win=%d\n", mssg->header.seq_number, mssg->header.ack_number, mssg->header.window);
  if (sendto(socket->sd, mssg, MICROTCP_RECVBUF_LEN, 0, address, address_len) == -1)
  {
    printf("Error in sending the message from socket <%d>\n", socket->sd);
    fprintf(stderr, "Error: %s\n", strerror(errno));
    return -1;
  }

  if (recvfrom(socket->sd, (void *restrict)rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&address_len) == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    return -1;
  }

  memcpy(mssg, rcv, sizeof(message_t));
  sleep(1);
  printf("Received ??, seq=%d, ack=%d\n", mssg->header.seq_number, mssg->header.ack_number);

  if (mssg->header.control != (ACK)) return -1;
  
  sleep(1);
  printf("Received ACK\n");
  
  // /*making next message TA FTIAXNOUME MESA STHN SEND*/
  // mssg->header.ack_number = mssg->header.seq_number + 1;
  // socket->ack_number = mssg->header.ack_number;
  // mssg->header.seq_number = socket->seq_number + 1;
  // socket->seq_number = mssg->header.seq_number;
  // mssg->header.control = mssg->header.control; /*we don't know the next header flags so there's no need to change the header.control*/

  socket->state = ESTABLISHED;

  /*allocate memory for recvbuf and initialize the window values accordingly*/
  socket->recvbuf = malloc(MICROTCP_RECVBUF_LEN);
  socket->curr_win_size = mssg->header.window;
  socket->cwnd = MICROTCP_INIT_CWND;
  socket->ssthresh = MICROTCP_INIT_SSTHRESH;

  return 0; /*successful acceptance*/
}

int
microtcp_shutdown (microtcp_sock_t *socket, int how)
{

  /*if the server receives a FIN ACK in microtcp_recv the server's state changes to "CLOSING BY PEER" and then we continue to shutdown*/
  if(socket->state == CLOSING_BY_PEER){

    message_t *server_mssg = malloc(sizeof(message_t));
    void *server_rcv = malloc(MICROTCP_RECVBUF_LEN);
    /*address and addrlen are variables to use in recvfrom so that the socket's addresses won't change*/
    struct sockaddr *address = malloc(sizeof(struct sockaddr));
    socklen_t addrlen = sizeof(struct sockaddr_in);

    memset(address, 0, sizeof(struct sockaddr));

    memset(server_mssg, 0, sizeof(server_mssg));
    memset(server_rcv, 0, sizeof(server_rcv));

    server_mssg->header.ack_number = socket->seq_number + 1;
    socket->ack_number = server_mssg->header.ack_number;
    server_mssg->header.control = ACK

    sleep(1);
    printf("Sending ACK, ack=%d\n", server_mssg->header.ack_number);
    if (sendto(socket->sd, server_mssg, MICROTCP_RECVBUF_LEN, 0, socket->destaddr, cl_len) == -1)
    {
      printf("Error in sending the message to client\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    sleep(1);
    printf("Server's state changed to CLOSING_BY_PEER\n");

    /*SEND ANYTHING LEFT?*/

    /*sequence nuumber is the next byte after sending anything left*/
    socket->seq_number = socket->seq_number + 1;

    server_mssg->header.seq_number = socket->seq_number;
    server_mssg->header.ack_number = socket->ack_number;
    server_mssg->header.control = (FIN | ACK);

    sleep(1);
    printf("Sending FIN ACK, seq=%d, ack=%d\n", server_mssg->header.seq_number, server_mssg->header.ack_number);
    if (sendto(socket->sd, server_mssg, MICROTCP_RECVBUF_LEN, 0, socket->destaddr, cl_len) == -1)
    {
      printf("Error in sending the message to client\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }


    if (recvfrom(socket->sd, (void *restrict)server_rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&addrlen) == -1)
    {
      printf("Error in receiving the message from client\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    memcpy(server_mssg, server_rcv, sizeof(message_t));

    sleep(1);
    printf("Received ??, seq=%d, ack=%d\n", server_mssg->header.seq_number, server_mssg->header.ack_number);

    if (server_mssg->header.control != (ACK)) return -1;
    sleep(1);
    printf("Received ACK\n");
    server_mssg->header.ack_number = server_mssg->header.seq_number + 1;
    socket->ack_number = server_mssg->header.ack_number;

    socket->state = CLOSED;
    sleep(1);
    printf("Server's state changed to CLOSED\n");
  } 
  else   /*client calls shutdown when he wants to terminate the connection*/
  {
    message_t *client_mssg = malloc(sizeof(message_t));
    void *client_rcv = malloc(MICROTCP_RECVBUF_LEN);
    /*address and addrlen are variables to use in recvfrom so that the socket's addresses won't change*/
    struct sockaddr *address = malloc(sizeof(struct sockaddr));
    socklen_t addrlen = sizeof(struct sockaddr_in);

    memset(address, 0, sizeof(struct sockaddr));

    memset(client_mssg, 0, sizeof(client_mssg));
    memset(client_rcv, 0, sizeof(client_rcv));

    client_mssg->header.control = (FIN | ACK);
    client_mssg->header.seq_number = socket->seq_number;

    sleep(1);
    printf("Sending FIN ACK, seq=%d\n", client_mssg->header.seq_number);
    if (sendto(socket->sd, client_mssg, MICROTCP_RECVBUF_LEN, 0, socket->destaddr, sr_len) == -1)
    {
      printf("Error in sending the message to server\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }


    if (recvfrom(socket->sd, (void *restrict)client_rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&addrlen) == -1)
    {
      printf("Error in receiving the message from server\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    memcpy(client_mssg, client_rcv, sizeof(message_t));

    sleep(1);
    printf("Received ??, seq=%d, ack=%d\n", client_mssg->header.seq_number, client_mssg->header.ack_number);

    if (client_mssg->header.control != (ACK)) return -1;

    sleep(1);
    printf("Received ACK\n");

    /*since the client does not send any other message now, we won't change the message information (seq and ack number, control)*/
    socket->ack_number = client_mssg->header.seq_number + 1;
    socket->seq_number = socket->seq_number + 1;

    socket->state = CLOSING_BY_HOST;
    sleep(1);
    printf("Client's state changed to CLOSING_BY_HOST\n");

    if (recvfrom(socket->sd, (void *restrict)client_rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&addrlen) == -1)
    {
      printf("Error in receiving the message from server\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    memcpy(client_mssg, client_rcv, sizeof(message_t));

    sleep(1);
    printf("Received ??, seq=%d, ack=%d\n", client_mssg->header.seq_number, client_mssg->header.ack_number);

    if (client_mssg->header.control != (FIN | ACK)) return -1;
    printf("Received FIN ACK\n");
    client_mssg->header.ack_number = client_mssg->header.seq_number + 1;
    socket->ack_number = client_mssg->header.ack_number;
    client_mssg->header.seq_number = socket->seq_number;  /*we increaced the socket sequence number when we received the ACK from the server*/
    client_mssg->header.control = client_mssg->header.control & (~(FIN)); /*we are "subtracting" the FIN flag to send only an ACK*/

    sleep(1);
    printf("Sending ACK, seq=%d, ack=%d\n", client_mssg->header.seq_number, client_mssg->header.ack_number);
    if (sendto(socket->sd, client_mssg, MICROTCP_RECVBUF_LEN, 0, socket->destaddr, sr_len) == -1)
    {
      printf("Error in sending the message to server\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    socket->state = CLOSED;

    sleep(1);
    printf("Client's state changed to CLOSED\n");

  }

  return 0;

}

ssize_t
microtcp_send (microtcp_sock_t *socket, const void *buffer, size_t length,
               int flags)
{
  socklen_t dest_len = sizeof(struct sockaddr);
  message_t mssg;
  size_t remaining;
  uint32_t checksum;
  int bytes_sent;
  size_t bytes_to_send;
  size_t data_sent;
  int chuncks;
  void *data_per_chunk = malloc(length);
  void *temp = malloc(length);
  
  /*initializations*/
  memset(&mssg, 0, sizeof(mssg));
  memset(&checksum, 0, sizeof(checksum));
  memset(data_per_chunk, 0, sizeof(data_per_chunk));
  memset(temp, 0, sizeof(temp));
  memset(bytes_to_send, 0, sizeof(bytes_to_send));
  memset(&data_sent, 0, sizeof(data_sent));

  memcpy(temp, buffer, length);

  /*devide into chuncks and send*/

  remaining = length;
  while(data_sent < length){

    bytes_to_send = min3(socket->curr_win_size, socket->cwnd, remaining);
    chuncks = bytes_to_send/MICROTCP_MSS;

    for(int i=0; i<chuncks ; i++){
      
      memcpy(data_per_chunk, temp, bytes_to_send);
      
      /*make header*/
      mssg.header.control = ACK;
      mssg.header.ack_number = socket->ack_number;
      mssg.header.seq_number = socket->seq_number;
      mssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
      mssg.data = data_per_chunk;

      /*making the error checking*/
      /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
      checksum = crc32(buffer, length);
      mssg.header.checksum = checksum;
      
      bytes_sent = sendto(socket->sd, &mssg, bytes_to_send, flags, socket->destaddr, dest_len);
      if (bytes_sent == -1)
      {
        printf("Error in sending the message to server\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        socket->packets_lost++;
        socket->bytes_lost += bytes_to_send;
        return -1;
      }

      /*sendto was successful, update socket's values to be used on next header*/
      socket->bytes_send += bytes_sent;
      socket->packets_send++;
      socket->seq_number += bytes_sent;
      //LOGIKA to window allazei mono sto receive kai edw den xreiazetai na peira3oume kapoia allh plhroforia tou socket
      //revisit this an den einai etsi

      /*we have sent the first bytes_sent bytes of data*/
      temp = (temp + bytes_sent);
    }

    /* Check if there is a semi - filled chunk*/
    if(bytes_to_send % MICROTCP_MSS){
      chunks++;

      memcpy(data_per_chunk, temp, bytes_to_send);
      
      /*make header*/
      mssg.header.control = ACK;
      mssg.header.ack_number = socket->ack_number;
      mssg.header.seq_number = socket->seq_number;
      mssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
      mssg.data = data_per_chunk;

      /*making the error checking*/
      /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
      checksum = crc32(buffer, length);
      mssg.header.checksum = checksum;
      
      bytes_sent = sendto(socket->sd, &mssg, bytes_to_send, flags, socket->destaddr, dest_len);
      if (bytes_sent == -1)
      {
        printf("Error in sending the message to server\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        socket->packets_lost++;
        socket->bytes_lost += bytes_to_send;
        return -1;
      }

      /*sendto was successful, update socket's values to be used on next header*/
      socket->bytes_send += bytes_sent;
      socket->packets_send++;
      socket->seq_number += bytes_sent;
      //LOGIKA to window allazei mono sto receive kai edw den xreiazetai na peira3oume kapoia allh plhroforia tou socket
      //revisit this an den einai etsi

      /*we have sent the first bytes_sent bytes of data*/
      temp = (temp + bytes_sent);
    }

    /* Get the ACKs */
    for(i = 0; i < chunks; i++){ 
      //receive acks
    }

    // Retransmissions  se periptwsh 3dACK 8a 3anakanoume memcpy sthn temp me starter point to buffer+ACKnumber gia remainig bytes.
    // Update window
    // Update congestion control

    remaining -= bytes_to_send;

    data_sent+=bytes_to_send;

  }

  //edw ginetai o flow kai congestion control mhxanismos

  return bytes_sent;
}

ssize_t
microtcp_recv (microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{
  struct sockaddr *src_address = malloc(sizeof(struct sockaddr));
  socklen_t src_len = sizeof(struct sockaddr);

  message_t *mssg = malloc(sizeof(message_t));
  memset(mssg, 0, sizeof(mssg));

  int bytes_received = recvfrom(socket->sd, (void *restrict)buffer, length, flags, (struct sockaddr *restrict)src_address, (socklen_t *restrict)&src_len);
  if (bytes_received == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    fprintf(stderr, "Error: %s\n", strerror(errno));
    return -1;
  }

  memcpy(mssg, buffer, sizeof(message_t));

  socket->bytes_received += bytes_received;
  socket->packets_received++;
  memccpy(socket->recvbuf, mssg, bytes_received);
  socket->buf_fill_level += bytes_received;

  if (mssg->header.control == (FIN|ACK)){
    socket->state = CLOSING_BY_PEER;
    return -1;
  }

  return bytes_received;
  //if received FIN ACK change state to CLOSING BY PEER and return -1
}


//mesa sto test to server, o server 8a elegxei ka8e recv gia -1 kai an einai -1 8a elegxei to state.
//an to state einai closing by peer kalei thn shut down.

/*our functions*/
int min3(int a, int b, int c){
  int min = a;

  if(b<min) min = b;
  if(c<min) min = c;

  return min;
}