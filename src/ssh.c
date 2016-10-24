/*
 *	
 * Copyright (c) 2016 Cisco Systems, Inc.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *   Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 
 *   Redistributions in binary form must reproduce the above
 *   copyright notice, this list of conditions and the following
 *   disclaimer in the documentation and/or other materials provided
 *   with the distribution.
 * 
 *   Neither the name of the Cisco Systems, Inc. nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * ssh.c
 *
 * Secure Shell (SSH) awareness for joy
 *
 */

#include <stdio.h>      /* for fprintf()           */
#include <ctype.h>      /* for isprint()           */
#include <stdint.h>     /* for uint32_t            */
#include <arpa/inet.h>  /* for ntohl()             */
#include <string.h>     /* for memset()            */
#include "ssh.h"     
#include "p2f.h"        /* for zprintf_ ...        */

void copy_printable_string(char *buf, 
			   unsigned int buflen, 
			   const void *data,
			   unsigned int datalen) {
  const char *d = data;

  while (buflen-- && datalen--) {
    if (!isprint(*d)) {
      break;
    }
      *buf++ = *d++;
  }

  *buf = 0; /* null terminate buffer */
}


/*
 * from http://www.iana.org/assignments/ssh-parameters/ssh-parameters.xhtml
 */
enum ssh_msg_type {
  SSH_MSG_DISCONNECT 	            = 1, 	
  SSH_MSG_IGNORE 		    = 2, 	
  SSH_MSG_UNIMPLEMENTED 	    = 3, 	
  SSH_MSG_DEBUG 		    = 4, 	
  SSH_MSG_SERVICE_REQUEST 	    = 5, 	
  SSH_MSG_SERVICE_ACCEPT 	    = 6, 	
  SSH_MSG_KEXINIT 		    = 20, 	
  SSH_MSG_NEWKEYS 		    = 21, 	
  SSH_MSG_USERAUTH_REQUEST 	    = 50, 	
  SSH_MSG_USERAUTH_FAILURE 	    = 51, 	
  SSH_MSG_USERAUTH_SUCCESS 	    = 52, 	
  SSH_MSG_USERAUTH_BANNER 	    = 53, 	
  SSH_MSG_USERAUTH_INFO_REQUEST     = 60, 	
  SSH_MSG_USERAUTH_INFO_RESPONSE    = 61,	
  SSH_MSG_GLOBAL_REQUEST 	    = 80,	
  SSH_MSG_REQUEST_SUCCESS 	    = 81,	
  SSH_MSG_REQUEST_FAILURE 	    = 82,	
  SSH_MSG_CHANNEL_OPEN 		    = 90,	
  SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,		
  SSH_MSG_CHANNEL_OPEN_FAILURE 	    = 92,	
  SSH_MSG_CHANNEL_WINDOW_ADJUST     = 93, 	
  SSH_MSG_CHANNEL_DATA 		    = 94,	
  SSH_MSG_CHANNEL_EXTENDED_DATA     = 95,	
  SSH_MSG_CHANNEL_EOF 		    = 96, 	
  SSH_MSG_CHANNEL_CLOSE 	    = 97, 	
  SSH_MSG_CHANNEL_REQUEST 	    = 98, 	
  SSH_MSG_CHANNEL_SUCCESS 	    = 99, 	
  SSH_MSG_CHANNEL_FAILURE 	    = 100
}; 	

/*
 * from RFC 4253:
 *   Each packet is in the following format:
 *
 *    uint32    packet_length
 *    byte      padding_length
 *    byte[n1]  payload; n1 = packet_length - padding_length - 1
 *    byte[n2]  random padding; n2 = padding_length
 *    byte[m]   mac (Message Authentication Code - MAC); m = mac_length
 *
 */
struct ssh_packet { 
  uint32_t      packet_length;
  unsigned char padding_length;
  unsigned char payload;
} __attribute__((__packed__));    

unsigned int ssh_packet_parse(const void *pkt, unsigned int datalen, unsigned char *msg_code) {
  const struct ssh_packet *ssh_packet = pkt;
  uint32_t length;

  if (datalen < sizeof(ssh_packet)) {
    return 0;
  }

  length = ntohl(ssh_packet->packet_length);
  if (length > 32768) {
    return 0;   /* indicate parse error */
  }
  *msg_code = ssh_packet->payload;

  return length - ssh_packet->padding_length - 5;
}

unsigned int decode_uint32(const void *data) {
  const uint32_t *x = data;
  
  return ntohl(*x);
}

/*
 * from RFC 4253 Section 7.1
 * 
 *    Key exchange begins by each side sending the following packet:
 *
 *    byte         SSH_MSG_KEXINIT
 *    byte[16]     cookie (random bytes)
 *    name-list    kex_algorithms
 *    name-list    server_host_key_algorithms
 *    name-list    encryption_algorithms_client_to_server
 *    name-list    encryption_algorithms_server_to_client
 *    name-list    mac_algorithms_client_to_server
 *    name-list    mac_algorithms_server_to_client
 *    name-list    compression_algorithms_client_to_server
 *    name-list    compression_algorithms_server_to_client
 *    name-list    languages_client_to_server
 *    name-list    languages_server_to_client
 *    boolean      first_kex_packet_follows
 *    uint32       0 (reserved for future extension)
 *
 */
void ssh_parse_kexinit(struct ssh *ssh, const void *data, unsigned int datalen) {
  unsigned int length;

  if (datalen < 16) {
    return;
  }
  memcpy(ssh->cookie, data, 16);
  data += 16;
  datalen -= 16;

  /* parse host_key_algorithm name-list */
  if (datalen < 4) { 
    return;
  }
  length = decode_uint32(data);
  if (length == 0) {
    return;
  }
  data += 4;
  datalen -= 4;
  copy_printable_string(ssh->host_key_algos, sizeof(ssh->host_key_algos), data, datalen);

}


/*
 * start of ssh feature functions
 */

inline void ssh_init(struct ssh *ssh) {
  ssh->role = role_unknown;
  ssh->protocol[0] = 0; /* null terminate string */
  memset(ssh->cookie, 0, sizeof(ssh->cookie));
  memset(ssh->host_key_algos, 0, sizeof(ssh->host_key_algos));
}

void ssh_update(struct ssh *ssh, 
		const void *data, 
		unsigned int len, 
		unsigned int report_ssh) {
  unsigned int length;
  unsigned char msg_code;

  if (len == 0) {
    return;        /* skip zero-length messages */
  }

  if (report_ssh) {

    if (ssh->role == role_unknown) {
      if (ssh->protocol[0] == 0) {   
	copy_printable_string(ssh->protocol, sizeof(ssh->protocol), data, len);
	ssh->role = role_client; /* ? */
      }
    }
    length = ssh_packet_parse(data, len, &msg_code);
    if (length == 0) {
      return;
    }
    switch (msg_code) {
    case SSH_MSG_KEXINIT:
      ssh_parse_kexinit(ssh, data + sizeof(struct ssh_packet), length);
      break;
    default:
      ; /* noop */
    }
    
  }

}

void ssh_print_json(const struct ssh *x1, const struct ssh *x2, zfile f) {

  if (x1->role != role_unknown) {
    zprintf(f, ",\"ssh\":{");
    if (x1->protocol[0] != 0) {
      zprintf(f, "\"protocol\":\"%s\"", x1->protocol);
      if (x1->cookie[0] != 0) {
	zprintf(f, ",\"cookie\":");
	zprintf_raw_as_hex(f, x1->cookie, sizeof(x1->cookie));
      }
      zprintf(f, ",\"host_key_algo\":\"%s\"", x1->host_key_algos);
    }
    zprintf(f, "}");
  }
  
}

void ssh_delete(struct ssh *ssh) { 
  /* no memory needs to be freed */
}

void ssh_unit_test() {
  struct ssh ssh;
  zfile output;

  output = zattach(stdout, "w");
  if (output == NULL) {
    fprintf(stderr, "error: could not initialize (possibly compressed) stdout for writing\n");
  }
  ssh_init(&ssh);
  ssh_update(&ssh, NULL, 1, 1);
  ssh_update(&ssh, NULL, 2, 1);
  ssh_update(&ssh, NULL, 3, 1);
  ssh_update(&ssh, NULL, 4, 1);
  ssh_update(&ssh, NULL, 5, 1);
  ssh_update(&ssh, NULL, 6, 1);
  ssh_update(&ssh, NULL, 7, 1);
  ssh_update(&ssh, NULL, 8, 1);
  ssh_update(&ssh, NULL, 9, 1);
  ssh_print_json(&ssh, NULL, output);
 
} 
