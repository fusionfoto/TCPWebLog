/*
//=============================================================================+
// File name   : tcpweblog_client.c
// Begin       : 2012-02-28
// Last Update : 2012-07-13
// Version     : 1.7.0
//
// Website     : https://github.com/fubralimited/TCPWebLog
//
// Description : This program accept log text lines as input and sends them to a
//               remote server via TCP connection.
//
// Author: Nicola Asuni
//
// (c) Copyright:
//               Fubra Limited
//               Manor Coach House
//               Church Hill
//               Aldershot
//               Hampshire
//               GU12 4RQ
//				 UK
//               http://www.fubra.com
//               support@fubra.com
//
// License:
//    Copyright (C) 2012-2012 Fubra Limited
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Affero General Public License as
//    published by the Free Software Foundation, either version 3 of the
//    License, or (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Affero General Public License for more details.
//
//    You should have received a copy of the GNU Affero General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
//    See LICENSE.TXT file for more information.
//=============================================================================+
*/

/*
TO COMPILE:
	gcc -O3 -g -pipe -Wp,-D_FORTIFY_SOURCE=2 -fexceptions -fstack-protector -fno-strict-aliasing -fwrapv -fPIC --param=ssp-buffer-size=4 -D_GNU_SOURCE -o tcpweblog_client.bin tcpweblog_client.c

USAGE EXAMPLES

	APACHE (configuration per virtual host)
		CustomLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log access.log 1 10.0.2.15 xhost\" combined
		ErrorLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log error.log 1 10.0.2.15 xhost\"

	APACHE SSL (configuration per virtual host)
		CustomLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log ssl.access.log 1 10.0.2.15 xhost\" combined
		ErrorLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log ssl.error.log 1 10.0.2.15 xhost\"

	APACHE (general CustomLog)
		# you must prefix the log format with \"%h %V\", for example:
		LogFormat \"%h %V %{X-Forwarded-For}i %l %u %t \\\"%r\\\" %>s %b \\\"%{Referer}i\\\" \\\"%{User-Agent}i\\\"\" common
		CustomLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log access.log 1 - -\" common

	VARNISHNCSA
		You must prefix the log format with \"%h %V\", for example:
		varnishncsa -F \"%h %V %{X-Forwarded-For}i %l %u %t \\\"%r\\\" %>s %b \\\"%{Referer}i\\\" \\\"%{User-Agent}i\\\"\" | /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log varnish.log 1 - -

NOTES:
	The maximum input line length is 65000 bytes.
	If using SELinux, run the following command to allow apache daemon to open network connections:
		setsebool -P httpd_can_network_connect=1
*/


#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * Size of buffer used to scan input data.
 */
#define BUFLEN 65536

/**
 * Character used to mark a processed line on cache file.
 */
#define LMARK 35 // '#' character

/**
 * Append the input string on a log file
 * @param s string to append on log.
 */
void appendlog(const char *s, const char *file) {
	FILE *fp = NULL;
	int ret = EOF;
	fp = fopen(file, "a");
	if (fp != NULL) {
		ret = fputs(s, fp);
		fclose(fp);
	}
	if (ret == EOF) {
		// output an error message
		perror("TCPWebLog-Client (appendlog)");
	}
}

/**
 * Main function.
 * @param argc Argument counter.
 * @param argv[] Array of arguments: IP address and Port number.
 */
int main(int argc, char *argv[]) {

	// decode arguments
	if (argc != 8) {
		perror("\
This program accept log text lines as input and sends them via TCP to the specified IP:PORT.\n\
You must provide 7 arguments:\n\
\t- remote_ip_address: the IP address of the listening remote log server;\n\
\t- remote_port: the TCP	port of the listening remote log server;\n\
\t- local_cache_file: the local cache file to temporarily store the logs when the TCP connection is not available;\n\
\t- logname: the last part of the log file name (i.e.: access.log);\n\
\t- cluster_number: the cluster number;\n\
\t- client_ip: the client (local) IP address;\n\
\t- client_hostname: the client (local) hostname.\n\
\n\
EXAMPLES:\n\
\n\
\tAPACHE (configuration per virtual host)\n\
\t\tCustomLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log access.log 1 10.0.2.15 xhost\" combined\n\
\t\tErrorLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log error.log 1 10.0.2.15 xhost\"\n\
\n\
\tAPACHE SSL (configuration per virtual host)\n\
\t\tCustomLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log ssl.access.log 1 10.0.2.15 xhost\" combined\n\
\t\tErrorLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log ssl.error.log 1 10.0.2.15 xhost\"\n\
\n\
\tAPACHE (general CustomLog)\n\
\t\t# you must prefix the log format with \"%h %V\", for example:\n\
\t\tLogFormat \"%h %V %{X-Forwarded-For}i %l %u %t \\\"%r\\\" %>s %b \\\"%{Referer}i\\\" \\\"%{User-Agent}i\\\"\" common\n\t\tCustomLog \"| /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log access.log 1 - -\" common\n\
\n\
\tVARNISHNCSA\n\
\t\tYou must prefix the log format with \"%h %V\", for example:\n\t\tvarnishncsa -F \"%h %V %{X-Forwarded-For}i %l %u %t \\\"%r\\\" %>s %b \\\"%{Referer}i\\\" \\\"%{User-Agent}i\\\"\" | /usr/bin/tcpweblog_client.bin 10.0.3.15 9940 /var/log/tcpweblog_cache.log varnish.log 1 - -\n\
\n\
\tIf using SELinux, run the following command to allow the Apache daemon to open network connections:\n\
\t\tsetsebool -P httpd_can_network_connect=1");
		exit(1);
	}

	// set input values

	// the IP address of the listening remote log server
	char *ipaddress = (char *)argv[1];

	// the TCP	port of the listening remote log server
	int port = atoi(argv[2]);

	// the local cache file to temporarily store the logs when the TCP connection is not available
	char *cachelog = (char *)argv[3];

	// the file name for log file
	char *logname = (char *)argv[4];

	// the cluster number
	int cluster = atoi(argv[5]);

	// the local IP address
	char *clientip = (char *)argv[6];

	// the local hostname
	char *clienthost = (char *)argv[7];


	// file pointer
	char *ch = NULL;

	// option for SOL_SOCKET
	int optval = 1;

	// buffer used to read input data
	char *rawbuf = (char *)malloc(65536);

	// length of the raw buffer
	size_t rblen = 0;

	// count read chars with getline
	int glen = 0;

	// buffer used for a single log line
	char buf[BUFLEN];

	// initialize buffers
	memset(buf, 0, BUFLEN);

	// file pointer for local cache
	FILE *fp;

	// lenght of the string to send
	int blen = 0;

	// current file pointer position (used for cache file)
	long int cpos = 0;

	// temporary file pointer position (used for cache file)
	long int tpos = 0;

	// used to track errors when sending cached content
	int cerr = 0;

	// socket
	int s = -1;

	// structure containing an Internet socket address for server: an address family (always AF_INET for our purposes), a port number, an IP address
	struct sockaddr_in6 si_server;

	// size of si_server
	int slen = sizeof(si_server);

	// use IPv6 internet address
	si_server.sin6_family = AF_INET6;

	// initialize the si_server structure filling it with binary zeros
	memset((char *) &si_server, 0, slen);

	// use IPv6 internet address
	si_server.sin6_family = AF_INET6;

	// set the IP address we want to bind to.
	inet_pton(si_server.sin6_family, ipaddress, &(si_server.sin6_addr));

	// set the port to listen to, and ensure the correct byte order
	si_server.sin6_port = htons(port);

	// check if the program is in loop mode
	int loopcontrol = 0;

	// forever
	while (loopcontrol < 2) {

		rblen = 0;

		// read one line at time from stdin
		if ((glen = getline(&rawbuf, &rblen, stdin)) != -1) {

			loopcontrol = 0;

			// try to open a TCP connection if not already open
			if (s <= 0) {
				// start of block of data : create a network socket.
				// AF_INET says that it will be an Internet socket.
				// SOCK_STREAM Provides sequenced, reliable, two-way, connection-based byte streams.
				if ((s = socket(si_server.sin6_family, SOCK_STREAM, 0)) == -1) {
					// print an error message
					perror("TCPWebLog-Client (socket)");
				} else {
					// set socket to listen only IPv6
					if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &optval, sizeof(optval)) == -1) {
						perror("TCPWebLog-Client (setsockopt : IPPROTO_IPV6 - IPV6_V6ONLY)");
					}
					// set SO_REUSEADDR on socket to true (1):
					if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
						perror("TCPWebLog-Client (setsockopt : SOL_SOCKET - SO_REUSEADDR)");
					}
					// establish a connection to the server
					if (connect(s, &si_server, slen) == -1) {
						close(s);
						s = -1;
						// print an error message
						perror("TCPWebLog-Client (connect)");
					}
				}
			}

			// add a prefix and source info to the log line
			sprintf(buf, "@@%s\t%d\t%s\t%s\t%s", logname, cluster, clientip, clienthost, rawbuf);

			if (s > 0) {

				// send line
				if (send(s, buf, strlen(buf), 0) == -1) {

					// output an error message
					perror("TCPWebLog-Client (sendto)");
					close(s);
					s = -1;

					// log the file on local cache
					appendlog(buf, cachelog);

				} else { // the line has been successfully sent

					// try to send log files on cache (if any)
					fp = fopen(cachelog, "rwb+");

					if (fp != NULL) {
						// get starting line position
						cpos = ftell(fp);
						cerr = 0;
						// send the cache logs
						while (fgets(buf, BUFLEN, fp) != NULL) {
							// check for valid lines
							if ((buf[0] == '@') && ((blen = strlen(buf)) > 10)) {
								// send line
								if (sendto(s, buf, blen, 0, NULL, 0) == -1) {
									// output an error message
									perror("TCPWebLog-Client (sendto)");
									// mark error
									cerr = 1;
									// close connection
									close(s);
									s = -1;
									// exit from while loop
									break;
								} else {
									// store the starting line position
									tpos = cpos;
									// get next line position
									cpos = ftell(fp);
									// move pointer at the beginning of the sent line
									fseek(fp, tpos, SEEK_SET);
									// mark line as sent
									fputc(LMARK, fp);
									// restore file pointer position
									fseek(fp, cpos, SEEK_SET);
								}
							}
						}
						if (cerr == 0) {
							// remove the cache file
							remove(cachelog);
						}
						fclose(fp);
					}

				} // end of else - when sending is working

			} else { // we do not have a valid socket

				// log the file on local cache
				appendlog(buf, cachelog);
			}

		} // end scan line

		++loopcontrol;

	} // end of while (1)

	// free resources
	close(s);
	free(rawbuf);
	free(ch);
	free(ipaddress);
	free(cachelog);

	// close program and return 0
	return 0;
}

//=============================================================================+
// END OF FILE
//=============================================================================+
