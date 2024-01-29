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

  message_t *mssg = malloc(sizeof(message_t));
  void *rcv = malloc(MICROTCP_RECVBUF_LEN);

  memset(mssg, 0, sizeof(message_t));
  
  /*make message*/
  mssg->header.seq_number = (uint32_t)(rand() % 10);
  socket->seq_number = mssg->header.seq_number;
  mssg->header.control = SYN;
  mssg->header.window = MICROTCP_WIN_SIZE;
  socket->init_win_size = MICROTCP_WIN_SIZE;

  printf("Sending SYN, seq=%d, win=%d\n", mssg->header.seq_number, mssg->header.window);

  if (sendto(socket->sd, (const void *)mssg, sizeof(message_t), 0, address, address_len) == -1)
  {
    printf("Error in sending the message from socket <%d>\n", socket->sd);
    return -1;
  }

  printf("MESSAGE SENT\n");

  if (recvfrom(socket->sd, (void *restrict)rcv, sizeof(message_t), 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&address_len) == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    return -1;
  }

  memcpy(mssg, rcv, sizeof(message_t));

  printf("Received ??, seq=%d, ack=%d\n", mssg->header.seq_number, mssg->header.ack_number);

  if (mssg->header.control != (SYN | ACK)) return -1;

  printf("Received SYN ACK with win=%d\n", mssg->header.window);

  socket->curr_win_size = mssg->header.window; /*fixing curr_win_size of client according to what the server sent to him*/

  /*making next message*/
  mssg->header.ack_number = mssg->header.seq_number + 1;
  socket->ack_number = mssg->header.ack_number;
  mssg->header.seq_number = socket->seq_number + 1;
  socket->seq_number = socket->seq_number + 1;
  mssg->header.window = MICROTCP_WIN_SIZE;
  mssg->header.control = mssg->header.control & (~(SYN)); /*we are "subtracting" the SYN flag*/

  printf("Sending ACK, seq=%d, ack=%d\n", mssg->header.seq_number, mssg->header.ack_number);
  if (sendto(socket->sd, mssg, sizeof(message_t), 0, address, address_len) == -1)
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
  memset(rcv, 0, sizeof(*rcv));

  memset(mssg, 0, sizeof(*mssg));

  cl = address; /*here the server knows the address of the client, so we can initialize the global variable cl*/
  socket->destaddr = address; /*which is also its destinaton address*/

  printf("WAITING TO ACCEPT\n");

  if (recvfrom(socket->sd, (void *restrict)rcv, sizeof(message_t), 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&address_len) == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    fprintf(stderr, "Error: %s\n", strerror(errno));
    return -1;
  }

  printf("MESSAGE RECEIVED\n");

  memcpy(mssg, rcv, sizeof(message_t));

  printf("Received ??, seq=%d, win=%d\n", mssg->header.seq_number, mssg->header.window);

  if (mssg->header.control != (SYN)) {
    printf("SYN: %d\nWe got: %d\n", SYN, mssg->header.window);  
    return -1;
  }

  printf("Received SYN with win=%d\n", mssg->header.window);

  /*make message*/
  mssg->header.ack_number = mssg->header.seq_number + 1;
  socket->ack_number = mssg->header.ack_number;
  mssg->header.seq_number = (uint32_t)(rand() % 10);
  socket->seq_number = mssg->header.seq_number;
  mssg->header.control = mssg->header.control | ACK; /*making the SYN ACK flag only with the |ACK because the header has already the SYN in it*/
  mssg->header.window = MICROTCP_WIN_SIZE;
  socket->init_win_size = mssg->header.window;

  printf("Sending SYN ACK, seq=%d, ack=%d, win=%d\n", mssg->header.seq_number, mssg->header.ack_number, mssg->header.window);
  if (sendto(socket->sd, mssg, sizeof(message_t), 0, address, address_len) == -1)
  {
    printf("Error in sending the message from socket <%d>\n", socket->sd);
    fprintf(stderr, "Error: %s\n", strerror(errno));
    return -1;
  }

  if (recvfrom(socket->sd, (void *restrict)rcv, sizeof(message_t), 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&address_len) == -1)
  {
    printf("Error in receiving the message in socket <%d>\n", socket->sd);
    return -1;
  }

  memcpy(mssg, rcv, sizeof(message_t));
  printf("Received ??, seq=%d, ack=%d\n", mssg->header.seq_number, mssg->header.ack_number);

  if (mssg->header.control != (ACK)) return -1;
  
  printf("Received ACK\n");

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

    memset(server_mssg, 0, sizeof(*server_mssg));
    memset(server_rcv, 0, sizeof(*server_rcv));

    server_mssg->header.ack_number = socket->seq_number + 1;
    socket->ack_number = server_mssg->header.ack_number;
    server_mssg->header.control = ACK;

    printf("Sending ACK, ack=%d\n", server_mssg->header.ack_number);
    if (sendto(socket->sd, server_mssg, sizeof(message_t), 0, socket->destaddr, cl_len) == -1)
    {
      printf("Error in sending the message to client\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    printf("Server's state changed to CLOSING_BY_PEER\n");

    // SEND ANYTHING LEFT

    /*sequence nuumber is the next byte after sending anything left*/
    socket->seq_number = socket->seq_number + 1;

    server_mssg->header.seq_number = socket->seq_number;
    server_mssg->header.ack_number = socket->ack_number;
    server_mssg->header.control = (FIN | ACK);

    printf("Sending FIN ACK, seq=%d, ack=%d\n", server_mssg->header.seq_number, server_mssg->header.ack_number);
    if (sendto(socket->sd, server_mssg, sizeof(message_t), 0, socket->destaddr, cl_len) == -1)
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

    printf("Received ??, seq=%d, ack=%d\n", server_mssg->header.seq_number, server_mssg->header.ack_number);

    if (server_mssg->header.control != (ACK)) return -1;
    
    printf("Received ACK\n");
    server_mssg->header.ack_number = server_mssg->header.seq_number + 1;
    socket->ack_number = server_mssg->header.ack_number;

    socket->state = CLOSED;
    
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

    memset(client_mssg, 0, sizeof(*client_mssg));
    memset(client_rcv, 0, sizeof(*client_rcv));

    client_mssg->header.control = (FIN | ACK);
    client_mssg->header.seq_number = socket->seq_number;

    printf("Sending FIN ACK, seq=%d\n", client_mssg->header.seq_number);
    if (sendto(socket->sd, client_mssg, sizeof(message_t), 0, socket->destaddr, sr_len) == -1)
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

    printf("Received ??, seq=%d, ack=%d\n", client_mssg->header.seq_number, client_mssg->header.ack_number);

    if (client_mssg->header.control != (ACK)) return -1;

    printf("Received ACK\n");

    /*since the client does not send any other message now, we won't change the message information (seq and ack number, control)*/
    socket->ack_number = client_mssg->header.seq_number + 1;
    socket->seq_number = socket->seq_number + 1;

    socket->state = CLOSING_BY_HOST;

    printf("Client's state changed to CLOSING_BY_HOST\n");

    if (recvfrom(socket->sd, (void *restrict)client_rcv, MICROTCP_RECVBUF_LEN, 0, (struct sockaddr *restrict)address, (socklen_t *restrict)&addrlen) == -1)
    {
      printf("Error in receiving the message from server\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    memcpy(client_mssg, client_rcv, sizeof(message_t));

    printf("Received ??, seq=%d, ack=%d\n", client_mssg->header.seq_number, client_mssg->header.ack_number);

    if (client_mssg->header.control != (FIN | ACK)) return -1;
    printf("Received FIN ACK\n");
    client_mssg->header.ack_number = client_mssg->header.seq_number + 1;
    socket->ack_number = client_mssg->header.ack_number;
    client_mssg->header.seq_number = socket->seq_number;  /*we increaced the socket sequence number when we received the ACK from the server*/
    client_mssg->header.control = client_mssg->header.control & (~(FIN)); /*we are "subtracting" the FIN flag to send only an ACK*/

    printf("Sending ACK, seq=%d, ack=%d\n", client_mssg->header.seq_number, client_mssg->header.ack_number);
    if (sendto(socket->sd, client_mssg, sizeof(message_t), 0, socket->destaddr, sr_len) == -1)
    {
      printf("Error in sending the message to server\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }

    socket->state = CLOSED;

    printf("Client's state changed to CLOSED\n");

  }

  return 0;

}


/*retransmition logic: Prepei na 3anasteiloume ta paketa apo to last ACKed paketo kai meta apo to ka8e batch dedomenwn ths while loop
 *Gia na to kanoume auto 3anastelnoume ta paketa kai meta peirazoume to i ths for pou xrhsimopoioume gia na lavoume ta ACKs katallhla wste sthn epomenh epanalhpsh tou
 *na perimenei pali gia to swsto ACK*/
ssize_t
microtcp_send (microtcp_sock_t *socket, const void *buffer, size_t length,
               int flags)
{
  socklen_t dest_len = sizeof(struct sockaddr);
  message_t sendmssg;
  size_t remaining;
  uint32_t checksum;
  int bytes_sent;
  int total_bytes_sent=0;
  size_t bytes_to_send;
  size_t data_sent;
  int chunks;
  void *data_per_chunk = malloc(length);
  void *temp = malloc(length);
  /*used for getting the acks*/
  int dupACKs=0;
  struct sockaddr *src_address = malloc(sizeof(struct sockaddr));
  socklen_t src_len = sizeof(struct sockaddr);
  int bytes_received;
  message_t recvmssg;
  size_t *chunk_seq_number;
  size_t *bytes_per_chunk;
  size_t prev_ack;
  bool ret = false; /*boolean to check for retransmits*/
  
  /*initializations*/
  memset(&sendmssg, 0, sizeof(sendmssg));
  memset(&checksum, 0, sizeof(checksum));
  memset(data_per_chunk, 0, sizeof(data_per_chunk));
  memset(temp, 0, sizeof(temp));
  memset(&bytes_to_send, 0, sizeof(bytes_to_send));
  memset(&data_sent, 0, sizeof(data_sent));
  memset(&recvmssg, 0, sizeof(recvmssg));
  prev_ack = socket->ack_number;

  memcpy(temp, buffer, length);

  /*set the receive timeout time with the code given*/
  struct timeval timeout;
  timeout. tv_sec = 0;
  timeout. tv_usec =MICROTCP_ACK_TIMEOUT_US; 
  if (setsockopt(socket->sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( struct timeval)) < 0) {
    perror(" setsockopt");
  }

  /*devide into chunks and send*/

  remaining = length;
  while(data_sent < length){
    
    bytes_to_send = min3(socket->curr_win_size, socket->cwnd, remaining);
    chunks = bytes_to_send/MICROTCP_MSS;
    chunk_seq_number = malloc(chunks*sizeof(size_t));
    bytes_per_chunk = malloc(chunks*sizeof(size_t));

    printf("bytes to send: %d\nchunks=%d\n", bytes_to_send, chunks);

    for(int i=0; i<chunks ; i++){
      
      memcpy(data_per_chunk, temp, bytes_to_send);
      
      /*make header*/
      sendmssg.header.control = ACK;
      sendmssg.header.ack_number = socket->ack_number;
      sendmssg.header.seq_number = socket->seq_number;
      sendmssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
      sendmssg.header.data_len = bytes_to_send; //TODO FIND DATALEN !!!!!!!!!!!!!!!!!!!//////////////////////////////////////////////////
      sendmssg.data = data_per_chunk;
      /*keep the sequence number of this chunk*/
      chunk_seq_number[i] = socket->seq_number;
      bytes_per_chunk[i] = bytes_to_send;

      /*compute checksum*/
      /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
      checksum = crc32(&sendmssg, sizeof(message_t));
      sendmssg.header.checksum = checksum;
      
      bytes_sent = sendto(socket->sd, &sendmssg, sizeof(message_t), flags, socket->destaddr, dest_len);
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
      total_bytes_sent += bytes_sent;
      socket->packets_send++;
      socket->seq_number += bytes_sent;
      socket->curr_win_size += bytes_sent;

      /*we have sent the first bytes_sent bytes of data*/
      temp = (temp + sendmssg.header.data_len);
    }

    /* Check if there is a semi - filled chunk*/
    if(bytes_to_send % MICROTCP_MSS){
      printf("INSIDE THE UNFILLED CHUNK: %d\n", bytes_to_send % MICROTCP_MSS);
      chunks++;
      chunk_seq_number = realloc(chunk_seq_number, chunks*sizeof(size_t));
      bytes_per_chunk = realloc(bytes_per_chunk, chunks*sizeof(size_t));

      memcpy(data_per_chunk, temp, bytes_to_send % MICROTCP_MSS);
      //printf("sizeof(*data_per_chunk)=%d\n", sizeof(*data_per_chunk));
      
      /*make header*/
      sendmssg.header.control = ACK;
      sendmssg.header.ack_number = socket->ack_number;
      sendmssg.header.seq_number = socket->seq_number;
      sendmssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
      sendmssg.data = data_per_chunk;
      sendmssg.header.data_len = sizeof(*data_per_chunk);
      chunk_seq_number[chunks-1] = socket->seq_number;
      bytes_per_chunk[chunks-1] = bytes_to_send % MICROTCP_MSS;

      /*compute checksum*/
      /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
      checksum = crc32(&sendmssg, sizeof(message_t));
      sendmssg.header.checksum = checksum;
      printf("sendingChecksum: %d\n", sendmssg.header.checksum);

      printf("Sending message %d with payload %d\n", sendmssg.data, sendmssg.header.data_len);
      printf("Sending ack=%d, control=%d, seq=%d, window=%d\n", sendmssg.header.ack_number, sendmssg.header.control, sendmssg.header.seq_number, sendmssg.header.window);
      
      bytes_sent = sendto(socket->sd, &sendmssg, sizeof(message_t), flags, socket->destaddr, dest_len);
      if (bytes_sent == -1)
      {
        printf("Error in sending the message to server\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        socket->packets_lost++;
        socket->bytes_lost += bytes_to_send;
        return -1;
      }

      printf("bytes_sent = %d\n", bytes_sent);

      /*sendto was successful, update socket's values to be used on next header*/
      socket->bytes_send += bytes_sent;
      total_bytes_sent += bytes_sent;
      socket->packets_send++;
      socket->seq_number += bytes_sent;
      socket->curr_win_size += bytes_sent;

      /*we have sent the first bytes_sent bytes of data, moving the head of the array will get us to the next byte of data that needs to be sent*/
      temp = (temp + sendmssg.header.data_len);
    }

    /* Get the ACKs */
    for(int i = 0; i < chunks; i++){ 
      bytes_received = recvfrom(socket->sd, (void *restrict)temp, MICROTCP_RECVBUF_LEN - socket->buf_fill_level, flags, (struct sockaddr *restrict)src_address, (socklen_t *restrict)&src_len);
      if(bytes_received < 0){
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          /*timeout*/
          fprintf(stderr, "Receive timeout occurred\n");
          socket->ssthresh = socket->cwnd/2;
          socket->cwnd = min(MICROTCP_MSS,socket->ssthresh);
          
          /*retransmit*/
          int k;
          /*for each packet after the one that i have to retransmit (including the packet that caused the retransmition)...*/
          for(k=i; k<chunks; k++){
            /*make header*/
            sendmssg.header.control = ACK;
            sendmssg.header.ack_number = socket->ack_number;
            sendmssg.header.seq_number = chunk_seq_number[k];
            sendmssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
            memcpy(sendmssg.data, (buffer + recvmssg.header.ack_number), bytes_per_chunk[k]);
            sendmssg.header.data_len = sizeof(*data_per_chunk);

            /*compute checksum*/
            /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
            checksum = crc32(&sendmssg, sizeof(message_t));
            sendmssg.header.checksum = checksum;
            
            bytes_sent = sendto(socket->sd, &sendmssg, sizeof(message_t), flags, socket->destaddr, dest_len);
            if (bytes_sent == -1)
            {
              printf("Error in sending the message to server\n");
              fprintf(stderr, "Error: %s\n", strerror(errno));
              socket->packets_lost++;
              socket->bytes_lost += bytes_to_send;
              return -1;
            }
          }
          /*decrease i so that in the next for loop it will remain the same as in this*/
          i--;
          prev_ack = recvmssg.header.ack_number;
          continue;
        } else {
          printf("Error in receiving the message in socket <%d>\n", socket->sd);
          fprintf(stderr, "Error: %s\n", strerror(errno));
          return -1;
        }
      }

      memcpy(&recvmssg, temp, sizeof(message_t));
      socket->curr_win_size -= recvmssg.header.data_len;
      socket->recvbuf = recvmssg.data;
      socket->buf_fill_level += recvmssg.header.data_len;

      if(recvmssg.header.window == 0){
        /*keep sending a special package with 0 payload until you receive an ACK with window!=0*/
        while(recvmssg.header.window==0){
          /*wait a rand ammount of time before sending the special package*/
          srand((unsigned int)time(NULL));
          unsigned int randomTime = rand() % (MICROTCP_ACK_TIMEOUT_US + 1);
          usleep(randomTime);

          memset(&sendmssg, 0, sizeof(sendmssg));
          sendmssg.header.ack_number = socket->ack_number;
          sendmssg.header.seq_number = recvmssg.header.ack_number;
          sendmssg.header.control = ACK;
          sendmssg.header.data_len = 0; /*payload=0*/

          printf("Sending special package with 0 payload\n");
          if (sendto(socket->sd, &sendmssg, sizeof(message_t), 0, socket->destaddr, dest_len) == -1)
          {
            printf("Error in sending the message to client\n");
            fprintf(stderr, "Error: %s\n", strerror(errno));
            return -1;
          }

          if(recvfrom(socket->sd, (void *restrict)temp, MICROTCP_RECVBUF_LEN - socket->buf_fill_level, flags, (struct sockaddr *restrict)src_address, (socklen_t *restrict)&src_len)==-1){
            printf("Error in receiving the message in socket <%d>\n", socket->sd);
            fprintf(stderr, "Error: %s\n", strerror(errno));
            return -1;
          }
          memcpy(&recvmssg, temp, sizeof(recvmssg));
        }
        
        /*retransmit*/
        int k;
        /*for each packet after the one that i have to retransmit (including the packet that caused the retransmition)...*/
        for(k=i; k<chunks; k++){
          /*make header*/
          sendmssg.header.control = ACK;
          sendmssg.header.ack_number = socket->ack_number;
          sendmssg.header.seq_number = chunk_seq_number[k];
          sendmssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
          memcpy(sendmssg.data, (buffer + recvmssg.header.ack_number), bytes_per_chunk[k]);
          sendmssg.header.data_len = sizeof(*data_per_chunk);

          /*compute checksum*/
          /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
          checksum = crc32(&sendmssg, sizeof(message_t));
          sendmssg.header.checksum = checksum;
          
          bytes_sent = sendto(socket->sd, &sendmssg, sizeof(message_t), flags, socket->destaddr, dest_len);
          if (bytes_sent == -1)
          {
            printf("Error in sending the message to server\n");
            fprintf(stderr, "Error: %s\n", strerror(errno));
            socket->packets_lost++;
            socket->bytes_lost += bytes_to_send;
            return -1;
          }
        }
        /*decrease i so that in the next for loop it will remain the same as in this*/
        i--;

      } else {
        /*congestion control*/
        if(recvmssg.header.ack_number == prev_ack){
          dupACKs++;
        } else {
          if(socket->cwnd<=socket->ssthresh){
            /*slow start*/
            socket->cwnd + MICROTCP_MSS;
          } else if (socket->cwnd > socket->ssthresh) {
            /*congestion avoidance*/
            socket->cwnd += MICROTCP_MSS * (MICROTCP_MSS/socket->cwnd);
          }

          if(dupACKs > 0) dupACKs = 0;
        }

        if(dupACKs==3){
          /*fast recovery*/
          fprintf(stderr, "3 duplicate ACKs occured, retransmit missing package");
          socket->ssthresh = socket->cwnd/2;
          socket->cwnd = socket->cwnd/2 + 1;

          /*retransmit*/
          int k;
          /*for each packet after the one that i have to retransmit (including the packet that caused the retransmition)...*/
          for(k=i; k<chunks; k++){
            /*make header*/
            sendmssg.header.control = ACK;
            sendmssg.header.ack_number = socket->ack_number;
            sendmssg.header.seq_number = chunk_seq_number[k];
            sendmssg.header.window = MICROTCP_RECVBUF_LEN - socket->buf_fill_level;
            memcpy(sendmssg.data, (buffer + recvmssg.header.ack_number), bytes_per_chunk[k]);
            sendmssg.header.data_len = sizeof(*data_per_chunk);

            /*compute checksum*/
            /*assuming that the buffer already has the header and the data ready to be sent on the buffer and has 0 on the CRC32 field of the header...*/
            checksum = crc32(&sendmssg, sizeof(message_t));
            sendmssg.header.checksum = checksum;
            
            bytes_sent = sendto(socket->sd, &sendmssg, sizeof(message_t), flags, socket->destaddr, dest_len);
            if (bytes_sent == -1)
            {
              printf("Error in sending the message to server\n");
              fprintf(stderr, "Error: %s\n", strerror(errno));
              socket->packets_lost++;
              socket->bytes_lost += bytes_to_send;
              return -1;
            }
          }
          /*decrease i so that in the next for loop it will remain the same as in this*/
          i--;

          /*make dupACKs counter 0 since the retransmition is done and wait for the correct ACK*/
          dupACKs = 0;
        }
      }

      prev_ack = recvmssg.header.ack_number;
    }

    remaining -= bytes_to_send;

    data_sent+=bytes_to_send;

  }
  return total_bytes_sent;
}

ssize_t
microtcp_recv (microtcp_sock_t *socket, void *buffer, size_t length, int flags)
{
  struct sockaddr *src_address = malloc(sizeof(struct sockaddr));
  socklen_t src_len = sizeof(struct sockaddr);

  message_t *recvmssg = malloc(sizeof(message_t));
  message_t *sendmssg = malloc(sizeof(message_t));
  uint32_t receivedChecksum, calculatedChecksum;
  int total_bytes_received=0;

  /*set the receive timeout time with the code given*/
  struct timeval timeout;
  timeout. tv_sec = 0;
  timeout. tv_usec =MICROTCP_ACK_TIMEOUT_US; 
  if (setsockopt(socket->sd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( struct timeval)) < 0) {
    perror("setsockopt");
  }

  memset(recvmssg, 0, sizeof(message_t));
  memset(sendmssg, 0, sizeof(message_t));
  memset(&receivedChecksum, 0, sizeof(receivedChecksum));
  memset(&calculatedChecksum, 0, sizeof(calculatedChecksum));

  /*receive the message*/

  int bytes_received = recvfrom(socket->sd, (void *restrict)buffer, length, flags, (struct sockaddr *restrict)src_address, (socklen_t *restrict)&src_len);
  if (bytes_received == -1)
  {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      /*timeout*/
      fprintf(stderr, "Receive timeout occurred\n");
      /*send dupACK*/
      sendmssg->header.ack_number = socket->ack_number;
      sendmssg->header.control = ACK;

      printf("Sending dupACK, ack=%d\n", sendmssg->header.ack_number);
      if (sendto(socket->sd, sendmssg, sizeof(message_t), 0, socket->destaddr, cl_len) == -1)
      {
        printf("Error in sending the message to client\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return -1;
      }
    } else {
      printf("Error in receiving the message in socket <%d>\n", socket->sd);
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }
  }

  /* print for debug: printf("bytes_received = %d\n", bytes_received);*/

  memcpy(recvmssg, buffer, sizeof(message_t));

  /*prints for debug*/
  // printf("Received message %d with payload %d\n", recvmssg->data, recvmssg->header.data_len);
  // printf("Received ack=%d, control=%d, seq=%d, window=%d\n", recvmssg->header.ack_number, recvmssg->header.control, recvmssg->header.seq_number, recvmssg->header.window);

  /*check checksum*/
  receivedChecksum = recvmssg->header.checksum;
  printf("receivedChecksum: %d\n", receivedChecksum);
  recvmssg->header.checksum=0;
  printf("header checksum: %d\n", recvmssg->header.checksum);
  calculatedChecksum = crc32(recvmssg, sizeof(message_t));
  printf("calculatedChecksum: %d\n", calculatedChecksum);

  if(receivedChecksum != calculatedChecksum){
    /*corrupted package, send dupACK*/
    fprintf(stderr, "Wrong checksum, package corrupted\n");
    sendmssg->header.ack_number = socket->ack_number;
    sendmssg->header.control = ACK;

    printf("Sending dupACK, ack=%d\n", sendmssg->header.ack_number);
    if (sendto(socket->sd, sendmssg, sizeof(message_t), 0, socket->destaddr, sizeof(struct sockaddr_in)) == -1)
    {
      printf("Error in sending the message to client\n");
      fprintf(stderr, "Error: %s\n", strerror(errno));
      return -1;
    }
  } else {
    /*correct checksum*/
    socket->bytes_received += bytes_received;
    total_bytes_received += bytes_received;
    socket->packets_received++;

    /*first check for FIN ACK*/
    if (recvmssg->header.control == (FIN|ACK)){
      socket->state = CLOSING_BY_PEER;
      /*call shutdown*/
      return -1;
    }

    if(recvmssg->header.seq_number == socket->ack_number){
      /*everything good, i got the correct package*/
      //printf("trww seg edw\n");
      memcpy(socket->recvbuf, recvmssg->data, recvmssg->header.data_len);   //????????
      socket->buf_fill_level += recvmssg->header.data_len;
      
      sendmssg->header.ack_number = recvmssg->header.seq_number + recvmssg->header.data_len;
      socket->ack_number = sendmssg->header.ack_number;
      sendmssg->header.window -= recvmssg->header.data_len;
      socket->curr_win_size -= recvmssg->header.data_len;
      sendmssg->header.control = ACK;
      
      printf("Sending ACK, ack=%d\n", sendmssg->header.ack_number);
      if (sendto(socket->sd, sendmssg, sizeof(message_t), 0, socket->destaddr, sizeof(struct sockaddr_in)) == -1)
      {
        printf("Error in sending the message to client\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return -1;
      }
    } else {
      /*i got the wrong package*/
      sendmssg->header.ack_number = socket->ack_number;
      sendmssg->header.control = ACK;

      printf("Sending dupACK, ack=%d\n", sendmssg->header.ack_number);
      if (sendto(socket->sd, sendmssg, sizeof(message_t), 0, socket->destaddr, sizeof(struct sockaddr_in)) == -1)
      {
        printf("Error in sending the message to client\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return -1;
      }
    }
  }

  return total_bytes_received;
}

/*our functions*/
size_t min3(size_t a, size_t b, size_t c){
  size_t min = a;

  if(b<min) min = b;
  if(c<min) min = c;

  return min;
}